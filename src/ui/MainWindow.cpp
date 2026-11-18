#include "ui/MainWindow.h"

#include "Settings.h"
#include "git/GitPtr.h"
#include "ui/Dialogs.h"
#include "ui/PrefsDialog.h"
#include "ui/RepoTab.h"
#include "ui/WelcomePage.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

namespace gitq {

namespace {
constexpr int MaxRecent = 20;
const QString kRecentKey = QStringLiteral("session/recent");
const QString kTabsKey = QStringLiteral("session/tabs");
const QString kCurrentKey = QStringLiteral("session/current");
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    buildUi();
    buildMenus();
    buildShortcuts();
    restoreGeometryState();
    m_restoring = true;   // don't overwrite the saved session before restoreSession() reads it
    onCurrentTabChanged(-1);
    m_restoring = false;
}

MainWindow::MainWindow(const QString &repoPath, const QStringList &args, QWidget *parent) : MainWindow(parent) {
    openRepository(repoPath, args);
}

MainWindow::~MainWindow() {
    // The tabs own widgets that live in the panes: delete them while the panes still exist.
    qDeleteAll(m_tabs);
    m_tabs.clear();
}

// ---------------------------------------------------------------------------
// Construction

void MainWindow::buildUi() {
    m_central = new QStackedWidget;
    setCentralWidget(m_central);
    m_welcome = new WelcomePage;
    m_welcome->setObjectName(QStringLiteral("welcomePage"));
    m_central->addWidget(m_welcome);
    connect(m_welcome, &WelcomePage::openRequested, this, &MainWindow::openFromUser);
    connect(m_welcome, &WelcomePage::browseRequested, this, &MainWindow::openRepositoryDialog);
    connect(m_welcome, &WelcomePage::removeRequested, this, [this](const QString &path) {
        removeRecent(path);
        m_welcome->setRecent(recentRepositories());
    });

    m_workspace = new QWidget;
    auto *wl = new QVBoxLayout(m_workspace);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    m_tabBar = new QTabBar;
    m_tabBar->setObjectName(QStringLiteral("repoTabs"));
    m_tabBar->setDocumentMode(true);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    wl->addWidget(m_tabBar);

    // The three panes are Visual Studio style dock widgets: drag a pane's tab to dock it next to
    // another, tab it with one, or float it. All repository tabs share one layout; each pane is a
    // stack with one page per tab.
    ads::CDockManager::setConfigFlags(ads::CDockManager::DefaultOpaqueConfig);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::TabsAtBottom, true);
    m_dockManager = new ads::CDockManager(m_workspace);
    wl->addWidget(m_dockManager, 1);
    m_central->addWidget(m_workspace);

    auto pane = [this](QStackedWidget *&stack, const QString &title, const char *name) {
        stack = new QStackedWidget;
        auto *dw = new ads::CDockWidget(m_dockManager, title);
        dw->setObjectName(QLatin1String(name));
        dw->setWidget(stack, ads::CDockWidget::ForceNoScrollArea);
        return dw;
    };
    // Default layout: commit list on top, diff and file list side by side below.
    m_commitsPane = pane(m_commitsStack, tr("Commits"), "commitsPane");
    m_diffPane = pane(m_diffStack, tr("Diff"), "diffPane");
    m_filesPane = pane(m_filesStack, tr("Files"), "filesPane");
    ads::CDockAreaWidget *topArea = m_dockManager->addDockWidget(ads::TopDockWidgetArea, m_commitsPane);
    ads::CDockAreaWidget *diffArea = m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_diffPane);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_filesPane, diffArea);
    m_dockManager->setSplitterSizes(topArea, {2000, 3000});
    m_dockManager->setSplitterSizes(diffArea, {3000, 1000});

    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::onCurrentTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        m_tabs.move(from, to);
        saveSession();
    });
    connect(m_tabBar, &QTabBar::customContextMenuRequested, this, &MainWindow::tabMenu);
}

void MainWindow::buildMenus() {
    auto onTab = [this](void (RepoTab::*fn)()) {
        return [this, fn] {
            if (RepoTab *t = currentTab()) (t->*fn)();
        };
    };
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Open repository..."), QKeySequence(Qt::CTRL | Qt::Key_O), this, &MainWindow::openRepositoryDialog);
    m_recentMenu = file->addMenu(tr("Open r&ecent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    m_tabActions << file->addAction(tr("&Close tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this,
                                    [this] { closeTab(m_tabBar->currentIndex()); });
    file->addSeparator();
    m_tabActions << file->addAction(tr("&Update"), QKeySequence(Qt::Key_F5), this, [this] {
        if (RepoTab *t = currentTab()) t->startLoad(true);
    });
    m_tabActions << file->addAction(tr("&Reload"), QKeySequence(Qt::SHIFT | Qt::Key_F5), this, [this] {
        if (RepoTab *t = currentTab()) t->startLoad(true);
    });
    m_tabActions << file->addAction(tr("Reread re&ferences"), this, onTab(&RepoTab::rereadRefs));
    m_tabActions << file->addAction(tr("&List references"), QKeySequence(Qt::Key_F2), this, onTab(&RepoTab::showRefsDialog));
    file->addSeparator();
    m_tabActions << file->addAction(tr("Start git &gui"), this, onTab(&RepoTab::startGitGui));
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q), this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Preferences"), this, &MainWindow::doPrefs);
    attachToPanes(file->actions());
    attachToPanes(edit->actions());

    m_viewMenu = menuBar()->addMenu(tr("&View"));

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About gitq"), this, [this] {
        QMessageBox::about(this, tr("About gitq"),
                           tr("<b>gitq</b> %1<br><br>A Qt reimplementation of gitk, the git repository "
                              "browser.<br>Uses libgit2 %2 and Qt %3.<br><br>Use and redistribute under the "
                              "terms of the GNU General Public License v2.")
                               .arg(QApplication::applicationVersion(), QStringLiteral(LIBGIT2_VERSION),
                                    QString::fromLatin1(qVersion())));
    });
    help->addAction(tr("&Key bindings"), this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(tr("Gitq key bindings"));
        box.setText(keyBindingsText());
        box.exec();
    });
}

void MainWindow::rebuildViewMenu() {
    RepoTab *t = currentTab();
    m_viewMenu->clear();
    QAction *a = m_viewMenu->addAction(tr("&New view..."), QKeySequence(Qt::SHIFT | Qt::Key_F4), this, [this] {
        if (RepoTab *t = currentTab()) t->newView();
    });
    a->setEnabled(t);
    a = m_viewMenu->addAction(tr("&Edit view..."), QKeySequence(Qt::Key_F4), this, [this] {
        if (RepoTab *t = currentTab()) t->editView();
    });
    a->setEnabled(t);
    a = m_viewMenu->addAction(tr("&Delete view"), this, [this] {
        if (RepoTab *t = currentTab()) t->deleteView();
    });
    a->setEnabled(t && t->currentView() > 0);
    m_viewMenu->addSeparator();
    delete m_viewGroup;
    m_viewGroup = new QActionGroup(this);
    if (t) {
        for (int i = 0; i < t->views().size(); ++i) {
            a = m_viewMenu->addAction(t->views()[i].name);
            a->setCheckable(true);
            a->setChecked(i == t->currentView());
            m_viewGroup->addAction(a);
            connect(a, &QAction::triggered, this, [this, i] {
                if (RepoTab *t = currentTab()) t->showView(i);
            });
        }
        m_viewMenu->addSeparator();
    }
    a = m_viewMenu->addAction(tr("&Multi-line commit list"), this, [this](bool on) {
        Settings &st = Settings::instance();
        st.commitListMultiline = on;
        st.save();
        for (RepoTab *t : std::as_const(m_tabs)) t->applySettings();
    });
    a->setCheckable(true);
    a->setChecked(Settings::instance().commitListMultiline);
    m_viewMenu->addSeparator();
    for (ads::CDockWidget *dw : {m_commitsPane, m_diffPane, m_filesPane}) m_viewMenu->addAction(dw->toggleViewAction());
    m_viewMenu->addAction(tr("&Reset layout"), this, [this] { m_dockManager->restoreState(m_defaultDockState, DockStateVersion); });
    attachToPanes(m_viewMenu->actions());
}

void MainWindow::rebuildRecentMenu() {
    m_recentMenu->clear();
    for (const QString &path : recentRepositories()) {
        if (!QFileInfo::exists(path)) continue;
        m_recentMenu->addAction(path, this, [this, path] { openFromUser(path); });
    }
    if (m_recentMenu->isEmpty()) {
        m_recentMenu->addAction(tr("No recent repositories"))->setEnabled(false);
        return;
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("&Clear list"), this, [this] {
        QSettings().remove(kRecentKey);
        if (m_tabs.isEmpty()) m_welcome->setRecent({});
    });
}

// Shortcuts of window-wide actions must also work while focus is in a floating pane, which is a
// separate top-level window: associate them with the panes too (an action's WindowShortcut fires
// when any of its widgets is in the active window).
void MainWindow::attachToPanes(const QList<QAction *> &actions) {
    for (QAction *a : actions) {
        if (a->shortcut().isEmpty()) continue;
        for (ads::CDockWidget *dw : {m_commitsPane, m_diffPane, m_filesPane}) dw->addAction(a);
    }
}

void MainWindow::buildShortcuts() {
    // Keys for the commit list, diff and file list are the tabs' own (RepoTab::buildShortcuts).
    auto key = [this](const QList<QKeySequence> &seqs, auto fn) {
        auto *a = new QAction(this);
        a->setShortcuts(seqs);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
        attachToPanes({a});
    };
    auto cycle = [this](int dir) {
        const int n = m_tabBar->count();
        if (n > 1) m_tabBar->setCurrentIndex((m_tabBar->currentIndex() + dir + n) % n);
    };
    key({Qt::CTRL | Qt::Key_Equal, Qt::CTRL | Qt::Key_Plus}, [this] { changeFontSize(1); });
    key({Qt::CTRL | Qt::Key_Minus}, [this] { changeFontSize(-1); });
    key({Qt::CTRL | Qt::Key_Tab}, [cycle] { cycle(1); });
    key({Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab, Qt::CTRL | Qt::SHIFT | Qt::Key_Tab}, [cycle] { cycle(-1); });
}

void MainWindow::restoreGeometryState() {
    QSettings q;
    if (!restoreGeometry(q.value(QStringLiteral("window/geometry")).toByteArray())) resize(1100, 800);
    m_defaultDockState = m_dockManager->saveState(DockStateVersion);
    QByteArray dock = q.value(QStringLiteral("window/dockState")).toByteArray();
    if (!dock.isEmpty() && !m_dockManager->restoreState(dock, DockStateVersion))
        m_dockManager->restoreState(m_defaultDockState, DockStateVersion);
    q.remove(QStringLiteral("window/vsplit"));   // pre-docking splitter layout
    q.remove(QStringLiteral("window/hsplit"));
}

void MainWindow::closeEvent(QCloseEvent *e) {
    QSettings q;
    q.setValue(QStringLiteral("window/geometry"), saveGeometry());
    q.setValue(QStringLiteral("window/dockState"), m_dockManager->saveState(DockStateVersion));
    if (RepoTab *t = currentTab()) q.setValue(QStringLiteral("window/listHeader"), t->listHeaderState());
    Settings::instance().save();
    saveSession();
    QMainWindow::closeEvent(e);
}

void MainWindow::changeEvent(QEvent *e) {
    if (e->type() == QEvent::ApplicationPaletteChange) {
        // Light/dark theme switch: move colours that follow the theme to the new defaults.
        Settings::instance().updateThemeColors();
        for (RepoTab *t : std::as_const(m_tabs)) t->settingsChanged(false);
    }
    QMainWindow::changeEvent(e);
}

// ---------------------------------------------------------------------------
// Tabs

RepoTab *MainWindow::currentTab() const { return m_tabs.value(m_tabBar->currentIndex()); }

void MainWindow::setCurrentTab(int index) {
    if (index >= 0 && index < m_tabBar->count()) m_tabBar->setCurrentIndex(index);
}

RepoTab *MainWindow::openRepository(const QString &path, const QStringList &args, const QString &prefix,
                                    bool makeCurrent) {
    Repository repo = Repository::open(path);
    const QString pre = args.isEmpty() ? QString() : prefix;   // only paths in args use it
    int index = int(m_tabs.size());
    bool replace = false;
    for (int i = 0; i < m_tabs.size(); ++i) {
        RepoTab *t = m_tabs[i];
        if (t->repoPath() != repo.path()) continue;
        if (args.isEmpty() || (t->args() == args && t->prefix() == pre)) {
            if (makeCurrent) setCurrentTab(i);
            if (!m_restoring) addRecent(t->displayPath());
            return t;
        }
        // Already open with other arguments: reopen it in place with these.
        makeCurrent |= m_tabBar->currentIndex() == i;
        index = i;
        replace = true;
        break;
    }
    auto *t = new RepoTab(std::move(repo), args, pre);
    addTab(t, index);
    if (replace) closeTab(index + 1);
    if (makeCurrent) setCurrentTab(index);
    if (!m_restoring) addRecent(t->displayPath());
    saveSession();
    return t;
}

void MainWindow::addTab(RepoTab *t, int index) {
    m_tabs.insert(index, t);
    m_commitsStack->addWidget(t);
    m_diffStack->addWidget(t->diffPane());
    m_filesStack->addWidget(t->filesPane());
    connect(t, &RepoTab::statusMessage, this, [this, t](const QString &text, int timeout) {
        if (t == currentTab()) statusBar()->showMessage(text, timeout);
    });
    // Queued: the view menu is rebuilt, and may be what emitted this.
    connect(t, &RepoTab::viewsChanged, this, [this, t] {
        if (t == currentTab()) rebuildViewMenu();
    }, Qt::QueuedConnection);
    connect(t, &RepoTab::raiseRequested, this, [this, t] {
        setCurrentTab(int(m_tabs.indexOf(t)));
        raise();
        activateWindow();
    });
    // May emit currentChanged (the first tab becomes current), so the tab must be in m_tabs.
    const int i = m_tabBar->insertTab(index, t->title());
    m_tabBar->setTabToolTip(i, t->displayPath());
}

void MainWindow::closeTab(int index) {
    RepoTab *t = m_tabs.value(index);
    if (!t) return;
    if (t->isActivated()) QSettings().setValue(QStringLiteral("window/listHeader"), t->listHeaderState());
    m_tabs.removeAt(index);
    m_tabBar->removeTab(index);   // switches the panes to another tab
    delete t;
    if (m_tabs.isEmpty()) onCurrentTabChanged(-1);
    saveSession();
}

void MainWindow::onCurrentTabChanged(int index) {
    RepoTab *t = m_tabs.value(index);
    if (t) {
        m_commitsStack->setCurrentWidget(t);
        m_diffStack->setCurrentWidget(t->diffPane());
        m_filesStack->setCurrentWidget(t->filesPane());
        setWindowTitle(QStringLiteral("gitq: %1").arg(t->title()));
        // Deferred, so that only the tab that ends up current loads when several are opened at once.
        QTimer::singleShot(0, this, [this] {
            if (RepoTab *c = currentTab()) c->activate();
        });
    } else {
        setWindowTitle(QStringLiteral("gitq"));
    }
    for (QAction *a : std::as_const(m_tabActions)) a->setEnabled(t);
    statusBar()->clearMessage();
    updateWelcome();
    rebuildViewMenu();
    saveSession();
}

void MainWindow::updateWelcome() {
    const bool hasTabs = !m_tabs.isEmpty();
    const bool showingPanes = m_central->currentWidget() == m_workspace;
    if (hasTabs && !showingPanes) {
        m_central->setCurrentWidget(m_workspace);
        m_dockManager->show();   // also brings back floating panes
    } else if (!hasTabs) {
        m_welcome->setRecent(recentRepositories());
        if (showingPanes) {
            m_dockManager->hideManagerAndFloatingWidgets();
            m_central->setCurrentWidget(m_welcome);
        }
    }
}

void MainWindow::tabMenu(const QPoint &pos) {
    const int index = m_tabBar->tabAt(pos);
    RepoTab *t = m_tabs.value(index);
    if (!t) return;
    QMenu menu(this);
    menu.addAction(tr("Close tab"), this, [this, t] { closeTab(int(m_tabs.indexOf(t))); });
    QAction *others = menu.addAction(tr("Close other tabs"), this, [this, t] {
        for (RepoTab *o : QList<RepoTab *>(m_tabs))
            if (o != t) closeTab(int(m_tabs.indexOf(o)));
    });
    others->setEnabled(m_tabs.size() > 1);
    menu.addSeparator();
    menu.addAction(tr("Copy path"), this, [t] { QApplication::clipboard()->setText(t->displayPath()); });
    menu.exec(m_tabBar->mapToGlobal(pos));
}

void MainWindow::openFromUser(const QString &path) {
    try {
        openRepository(path);
    } catch (const GitError &e) {
        QMessageBox::warning(this, tr("Open repository"),
                             tr("%1 is not a git repository.\n\n%2").arg(QDir::toNativeSeparators(path), e.message()));
        removeRecent(path);
        if (m_tabs.isEmpty()) m_welcome->setRecent(recentRepositories());
    }
}

void MainWindow::openRepositoryDialog() {
    QString start = QDir::homePath();
    if (RepoTab *t = currentTab()) start = QFileInfo(t->displayPath()).absolutePath();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open repository"), start);
    if (!dir.isEmpty()) openFromUser(dir);
}

// ---------------------------------------------------------------------------
// Session

void MainWindow::restoreSession() {
    QSettings q;
    struct Entry {
        QString path, prefix;
        QStringList args;
    };
    QList<Entry> entries;
    const int n = q.beginReadArray(kTabsKey);
    for (int i = 0; i < n; ++i) {
        q.setArrayIndex(i);
        entries.append({q.value(QStringLiteral("path")).toString(), q.value(QStringLiteral("prefix")).toString(),
                        q.value(QStringLiteral("args")).toStringList()});
    }
    q.endArray();
    const int current = q.value(kCurrentKey, 0).toInt();

    m_restoring = true;
    RepoTab *currentTab = nullptr;
    for (int i = 0; i < entries.size(); ++i) {
        try {
            RepoTab *t = openRepository(entries[i].path, entries[i].args, entries[i].prefix, false);
            if (i == current) currentTab = t;
        } catch (const GitError &) {
            // The repository is gone: drop the tab.
        }
    }
    m_restoring = false;
    if (currentTab) setCurrentTab(int(m_tabs.indexOf(currentTab)));
    saveSession();
}

void MainWindow::saveSession() const {
    if (m_restoring) return;
    QSettings q;
    q.remove(kTabsKey);
    q.beginWriteArray(kTabsKey, int(m_tabs.size()));
    for (int i = 0; i < m_tabs.size(); ++i) {
        q.setArrayIndex(i);
        q.setValue(QStringLiteral("path"), m_tabs[i]->displayPath());
        if (!m_tabs[i]->args().isEmpty()) {
            q.setValue(QStringLiteral("args"), m_tabs[i]->args());
            q.setValue(QStringLiteral("prefix"), m_tabs[i]->prefix());
        }
    }
    q.endArray();
    q.setValue(kCurrentKey, m_tabBar->currentIndex());
}

QStringList MainWindow::recentRepositories() { return QSettings().value(kRecentKey).toStringList(); }

void MainWindow::addRecent(const QString &path) {
    QStringList recent = recentRepositories();
    recent.removeAll(path);
    recent.prepend(path);
    QSettings().setValue(kRecentKey, recent.mid(0, MaxRecent));
}

void MainWindow::removeRecent(const QString &path) {
    QStringList recent = recentRepositories();
    recent.removeAll(path);
    QSettings().setValue(kRecentKey, recent);
}

// ---------------------------------------------------------------------------
// Settings

void MainWindow::doPrefs() {
    PrefsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    Settings &st = Settings::instance();
    Settings n = dlg.result();
    n.views = st.views;
    bool reload = n.showLocalChanges != st.showLocalChanges;
    st = n;
    st.save();
    for (RepoTab *t : std::as_const(m_tabs)) t->settingsChanged(reload);
}

void MainWindow::changeFontSize(int delta) {
    Settings &st = Settings::instance();
    for (QFont *f : {&st.mainFont, &st.textFont}) {
        int size = f->pointSize() > 0 ? f->pointSize() : 10;
        f->setPointSize(qMax(4, size + delta));
    }
    for (RepoTab *t : std::as_const(m_tabs)) t->applySettings();
}

} // namespace gitq
