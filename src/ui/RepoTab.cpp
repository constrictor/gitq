#include "ui/RepoTab.h"

#include "git/Blame.h"
#include "git/Ops.h"
#include "model/CommitModel.h"
#include "model/FileListModel.h"
#include "ui/BlameWindow.h"
#include "ui/CommitListView.h"
#include "ui/Dialogs.h"
#include "ui/GraphDelegate.h"
#include "ui/ViewDialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace gitq {

RepoTab::RepoTab(Repository repo, const QStringList &args, const QString &prefix, QWidget *parent)
    : QWidget(parent), m_repo(std::move(repo)), m_args(args), m_prefix(prefix) {
    m_repoPath = m_repo.path();

    // View 0: "All files" (command line revisions without paths); view 1: full command line if it has paths.
    ViewDef all;
    all.name = tr("All files");
    int dd = int(args.indexOf(QStringLiteral("--")));
    all.args = dd >= 0 ? args.mid(0, dd) : args;
    m_views << all;
    QString err;
    LogOptions parsed = RevArgs::parse(args, &m_repo, &err);
    if (!parsed.paths.isEmpty()) {
        ViewDef cmd;
        cmd.name = tr("Command line");
        cmd.args = args;
        if (dd < 0) {   // paths were recognised without "--": normalise
            QStringList revArgs;
            for (const QString &a : args)
                if (!parsed.paths.contains(a)) revArgs << a;
            m_views[0].args = revArgs;
            cmd.args = revArgs;
            cmd.args << QStringLiteral("--") << parsed.paths;
        }
        m_views << cmd;
        m_currentView = 1;
    }
    for (const ViewDef &v : Settings::instance().views) m_views << v;

    m_model = new CommitModel(this);
    m_model->setDateFormat(Settings::instance().dateFormat);
    m_loader = new LogLoader(this);
    m_searcher = new SearchWorker(this);
    m_fileModel = new FileListModel(this);

    buildUi();
    buildShortcuts();
    QByteArray hdr = QSettings().value(QStringLiteral("window/listHeader")).toByteArray();
    if (!hdr.isEmpty()) m_list->header()->restoreState(hdr);
    applySettings();

    connect(m_loader, &LogLoader::commitsLoaded, this, &RepoTab::onCommitsLoaded);
    connect(m_loader, &LogLoader::finished, this, &RepoTab::onLoadFinished);
    connect(m_searcher, &SearchWorker::matched, this, [this](const QList<Oid> &ids) { m_model->addHighlighted(ids); });
    connect(m_searcher, &SearchWorker::finished, this, [this] { m_contentSearchDone = true; });
    connect(&m_diffWatcher, &QFutureWatcher<DiffJob>::finished, this, &RepoTab::onDiffReady);
    connect(&m_nearbyWatcher, &QFutureWatcher<NearbyRefs>::finished, this, [this] {
        NearbyRefs n = m_nearbyWatcher.result();
        if (n.id == m_selected && !m_rangeDiff) m_diff->showNearby(n, Settings::instance().maxRefs);
    });

    if (!err.isEmpty()) {
        // Shown once the tab is in a window.
        QTimer::singleShot(0, this, [this, err] { emit statusMessage(err, 10000); });
    }
}

RepoTab::~RepoTab() {
    m_loader->cancel();
    m_searcher->cancel();
    if (m_nearbyCancel) m_nearbyCancel->store(true);
    m_diffWatcher.waitForFinished();
    m_nearbyWatcher.waitForFinished();
    delete m_diffPane;
    delete m_filesPane;
}

QString RepoTab::displayPath() const {
    QString p = m_repo.workdir().isEmpty() ? m_repoPath : m_repo.workdir();
    return QDir::cleanPath(QFileInfo(p).absoluteFilePath());
}

QString RepoTab::title() const { return QFileInfo(displayPath()).fileName(); }

void RepoTab::activate() {
    if (m_activated) return;
    m_activated = true;
    startLoad(false);
}

QByteArray RepoTab::listHeaderState() const { return m_list->header()->saveState(); }

// ---------------------------------------------------------------------------
// Construction

void RepoTab::buildUi() {
    // --- commits pane: commit list + bars (this widget)
    auto *tl = new QVBoxLayout(this);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(2);
    m_list = new CommitListView;
    m_list->setObjectName(QStringLiteral("commitList"));
    m_list->setModel(m_model);
    m_delegate = new GraphDelegate(m_model, m_list);
    m_list->setGraphDelegate(m_delegate);
    m_list->header()->setSectionResizeMode(CommitModel::SubjectColumn, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(CommitModel::AuthorColumn, QHeaderView::Interactive);
    m_list->header()->setSectionResizeMode(CommitModel::DateColumn, QHeaderView::Interactive);
    m_list->header()->resizeSection(CommitModel::AuthorColumn, 180);
    m_list->header()->resizeSection(CommitModel::DateColumn, 160);
    tl->addWidget(m_list, 1);

    auto *bar = new QHBoxLayout;
    auto *shaLabel = new QLabel(tr("SHA1 ID:"));
    m_shaEdit = new QLineEdit;
    m_shaEdit->setObjectName(QStringLiteral("shaEdit"));
    m_shaEdit->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QLatin1Char('0')) * 42);
    m_backBtn = new QToolButton;
    m_backBtn->setArrowType(Qt::LeftArrow);
    m_backBtn->setToolTip(tr("Back"));
    m_fwdBtn = new QToolButton;
    m_fwdBtn->setArrowType(Qt::RightArrow);
    m_fwdBtn->setToolTip(tr("Forward"));
    m_rowLabel = new QLabel(tr("Row"));
    m_rowLabel->setObjectName(QStringLiteral("rowLabel"));
    m_progress = new QProgressBar;
    m_progress->setRange(0, 0);
    m_progress->setMaximumWidth(150);
    m_progress->setTextVisible(false);
    m_progress->hide();
    bar->addWidget(shaLabel);
    bar->addWidget(m_shaEdit);
    bar->addWidget(m_backBtn);
    bar->addWidget(m_fwdBtn);
    bar->addSpacing(10);
    bar->addWidget(m_rowLabel);
    bar->addStretch(1);
    bar->addWidget(m_progress);
    tl->addLayout(bar);

    auto *fbar = new QHBoxLayout;
    fbar->addWidget(new QLabel(tr("Find")));
    m_findDownBtn = new QToolButton;
    m_findDownBtn->setArrowType(Qt::DownArrow);
    m_findDownBtn->setToolTip(tr("Find next (older)"));
    m_findUpBtn = new QToolButton;
    m_findUpBtn->setArrowType(Qt::UpArrow);
    m_findUpBtn->setToolTip(tr("Find previous (newer)"));
    fbar->addWidget(m_findDownBtn);
    fbar->addWidget(m_findUpBtn);
    fbar->addWidget(new QLabel(tr("commit")));
    m_findType = new QComboBox;
    m_findType->setObjectName(QStringLiteral("findType"));
    m_findType->addItems({tr("containing:"), tr("touching paths:"), tr("adding/removing string:"),
                          tr("changing lines matching:")});
    fbar->addWidget(m_findType);
    m_findEdit = new QLineEdit;
    m_findEdit->setObjectName(QStringLiteral("findEdit"));
    m_findEdit->setClearButtonEnabled(true);
    fbar->addWidget(m_findEdit, 1);
    m_findMode = new QComboBox;
    m_findMode->setObjectName(QStringLiteral("findMode"));
    m_findMode->addItems({tr("Exact"), tr("IgnCase"), tr("Regexp")});
    fbar->addWidget(m_findMode);
    m_findField = new QComboBox;
    m_findField->setObjectName(QStringLiteral("findField"));
    m_findField->addItems({tr("All fields"), tr("Headline"), tr("Comments"), tr("Author"), tr("Committer")});
    fbar->addWidget(m_findField);
    tl->addLayout(fbar);

    // --- diff pane
    m_diffPane = new QWidget;
    auto *ll = new QVBoxLayout(m_diffPane);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(2);
    auto *sbar = new QHBoxLayout;
    auto *searchBtn = new QPushButton(tr("Search"));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName(QStringLiteral("searchEdit"));
    m_searchEdit->setClearButtonEnabled(true);
    sbar->addWidget(searchBtn);
    sbar->addWidget(m_searchEdit, 1);
    ll->addLayout(sbar);
    auto *mbar = new QHBoxLayout;
    m_modeDiff = new QRadioButton(tr("Diff"));
    m_modeOld = new QRadioButton(tr("Old version"));
    m_modeNew = new QRadioButton(tr("New version"));
    m_modeDiff->setChecked(true);
    m_contextSpin = new QSpinBox;
    m_contextSpin->setObjectName(QStringLiteral("contextSpin"));
    m_contextSpin->setRange(0, 1000);
    m_contextSpin->setValue(Settings::instance().diffContext);
    m_ignoreSpace = new QCheckBox(tr("Ignore space change"));
    m_ignoreSpace->setChecked(Settings::instance().ignoreSpace);
    m_wordDiff = new QComboBox;
    m_wordDiff->setObjectName(QStringLiteral("wordDiff"));
    m_wordDiff->addItems({tr("Line diff"), tr("Markup words"), tr("Color words")});
    m_wordDiff->setCurrentIndex(Settings::instance().wordDiff);
    m_submoduleMode = new QComboBox;
    m_submoduleMode->setObjectName(QStringLiteral("submoduleMode"));
    m_submoduleMode->addItems({tr("Summary"), tr("Commits"), tr("Full diff")});
    m_submoduleMode->setToolTip(tr("Submodule changes: the commits recorded, the commits in between (click "
                                   "\"show files\" for their changes), or every file change inside"));
    m_submoduleMode->setCurrentIndex(qBound(0, Settings::instance().submoduleDiff, 2));
    for (QWidget *w : {static_cast<QWidget *>(m_modeDiff), static_cast<QWidget *>(m_modeOld),
                       static_cast<QWidget *>(m_modeNew)})
        mbar->addWidget(w);
    mbar->addSpacing(10);
    mbar->addWidget(new QLabel(tr("Lines of context:")));
    mbar->addWidget(m_contextSpin);
    mbar->addWidget(m_ignoreSpace);
    mbar->addWidget(m_wordDiff);
    mbar->addSpacing(10);
    mbar->addWidget(new QLabel(tr("Submodules:")));
    mbar->addWidget(m_submoduleMode);
    mbar->addStretch(1);
    ll->addLayout(mbar);
    m_diff = new DiffView;
    m_diff->setObjectName(QStringLiteral("diffView"));
    ll->addWidget(m_diff, 1);

    // --- files pane
    m_filesPane = new QWidget;
    auto *rl = new QVBoxLayout(m_filesPane);
    rl->setContentsMargins(0, 0, 0, 0);
    auto *rbar = new QHBoxLayout;
    m_patchMode = new QRadioButton(tr("Patch"));
    m_treeMode = new QRadioButton(tr("Tree"));
    m_treeMode->setObjectName(QStringLiteral("treeMode"));
    m_patchMode->setObjectName(QStringLiteral("patchMode"));
    m_patchMode->setChecked(true);
    rbar->addWidget(m_patchMode);
    rbar->addWidget(m_treeMode);
    rbar->addStretch(1);
    rl->addLayout(rbar);
    m_files = new QListView;
    m_files->setObjectName(QStringLiteral("fileList"));
    m_files->setModel(m_fileModel);
    m_files->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_files->setContextMenuPolicy(Qt::CustomContextMenu);
    m_files->setUniformItemSizes(true);
    rl->addWidget(m_files, 1);

    // --- wiring
    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &cur) { onCurrentRowChanged(cur.isValid() ? cur.row() : -1); });
    connect(m_list, &CommitListView::rowContextMenu, this, &RepoTab::rowMenu);
    connect(m_list, &CommitListView::refContextMenu, this, &RepoTab::refMenu);
    connect(m_shaEdit, &QLineEdit::returnPressed, this, &RepoTab::gotoCommitFromField);
    connect(m_backBtn, &QToolButton::clicked, this, [this] { goHistory(-1); });
    connect(m_fwdBtn, &QToolButton::clicked, this, [this] { goHistory(1); });

    m_findTimer = new QTimer(this);
    m_findTimer->setSingleShot(true);
    m_findTimer->setInterval(300);
    connect(m_findTimer, &QTimer::timeout, this, &RepoTab::runFind);
    connect(m_findEdit, &QLineEdit::textChanged, this, &RepoTab::findChanged);
    connect(m_findType, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_findField->setEnabled(i == 0);
        findChanged();
    });
    connect(m_findMode, &QComboBox::currentIndexChanged, this, &RepoTab::findChanged);
    connect(m_findField, &QComboBox::currentIndexChanged, this, &RepoTab::findChanged);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { findNext(1); });
    connect(m_findDownBtn, &QToolButton::clicked, this, [this] { findNext(1); });
    connect(m_findUpBtn, &QToolButton::clicked, this, [this] { findNext(-1); });

    connect(searchBtn, &QPushButton::clicked, this, [this] { m_diff->searchNext(false); });
    connect(m_searchEdit, &QLineEdit::textChanged, m_diff, &DiffView::setSearchText);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] { m_diff->searchNext(false); });

    auto redisplay = [this] {
        Settings &st = Settings::instance();
        st.diffContext = m_contextSpin->value();
        st.ignoreSpace = m_ignoreSpace->isChecked();
        st.wordDiff = m_wordDiff->currentIndex();
        st.submoduleDiff = m_submoduleMode->currentIndex();
        startDiff();
    };
    connect(m_contextSpin, &QSpinBox::valueChanged, this, redisplay);
    connect(m_ignoreSpace, &QCheckBox::toggled, this, redisplay);
    connect(m_wordDiff, &QComboBox::currentIndexChanged, this, redisplay);
    connect(m_submoduleMode, &QComboBox::currentIndexChanged, this, redisplay);
    for (QRadioButton *b : {m_modeDiff, m_modeOld, m_modeNew})
        connect(b, &QRadioButton::toggled, this, [this, redisplay](bool on) {
            if (on) redisplay();
        });
    connect(m_patchMode, &QRadioButton::toggled, this, [this](bool) { fillFileList(); });

    connect(m_files, &QListView::clicked, this, &RepoTab::onFileClicked);
    connect(m_files, &QListView::customContextMenuRequested, this, &RepoTab::fileMenu);
    connect(m_diff, &DiffView::commitLinkActivated, this, [this](const Oid &id) {
        if (!selectCommit(id)) emit statusMessage(tr("Commit %1 is not in the current view").arg(id.shortHex(10)), 5000);
    });
    connect(m_diff, &DiffView::submoduleToggled, this, [this](const QString &path) {
        if (!m_expandedSubmodules.remove(path)) m_expandedSubmodules.insert(path);
        startDiff();
    });
    connect(m_diff, &DiffView::submoduleOpenRequested, this, &RepoTab::openSubmodule);
    connect(m_diff, &DiffView::fileAtTopChanged, this, [this](const QString &path) {
        if (!m_patchMode->isChecked()) return;
        int r = path.isEmpty() ? 0 : m_fileModel->rowOfPath(path);
        if (r >= 0) {
            QSignalBlocker b(m_files->selectionModel());
            m_files->setCurrentIndex(m_fileModel->index(r));
        }
    });
    connect(m_diff, &DiffView::diffContextMenu, this, &RepoTab::diffMenu);
}

void RepoTab::buildShortcuts() {
    // Window-wide keys. The actions belong to the three pane contents so that they also work while
    // focus is in a floating pane (a separate top-level window); a WindowShortcut fires when one of
    // its widgets is visible in the active window, so only the tab on display reacts.
    auto key = [this](const QKeySequence &seq, auto fn) {
        auto *a = new QAction(this);
        a->setShortcut(seq);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, fn);
        for (QWidget *w : {static_cast<QWidget *>(this), m_diffPane.data(), m_filesPane.data()}) w->addAction(a);
    };
    auto keyNav = [this](const QKeySequence &seq, auto fn) {
        for (QWidget *w : {static_cast<QWidget *>(m_list), static_cast<QWidget *>(m_diff), static_cast<QWidget *>(m_files)}) {
            auto *s = new QShortcut(seq, w);
            s->setContext(Qt::WidgetWithChildrenShortcut);
            connect(s, &QShortcut::activated, this, fn);
        }
    };
    auto diffScroll = [this](int lines) {
        QScrollBar *sb = m_diff->verticalScrollBar();
        sb->setValue(sb->value() + lines);
    };
    auto diffPage = [this](int dir) {
        QScrollBar *sb = m_diff->verticalScrollBar();
        sb->setValue(sb->value() + dir * sb->pageStep());
    };
    auto listScroll = [this](int units) {
        QScrollBar *sb = m_list->verticalScrollBar();
        sb->setValue(sb->value() + units);
    };
    auto nextFile = [this](int dir) {
        const auto &entries = m_fileModel->entries();
        QString cur = m_diff->fileAtTop();
        int idx = cur.isEmpty() ? 0 : m_fileModel->rowOfPath(cur);
        int target = idx + dir;
        if (target < 0 || target >= entries.size()) return;
        if (entries[target].path.isEmpty()) m_diff->scrollToTop();
        else m_diff->scrollToFile(entries[target].path);
    };

    key(Qt::Key_P, [this] { moveSelection(-1); });
    key(Qt::Key_K, [this] { moveSelection(-1); });
    key(Qt::Key_N, [this] { moveSelection(1); });
    key(Qt::Key_J, [this] { moveSelection(1); });
    key(Qt::Key_Z, [this] { goHistory(-1); });
    key(Qt::Key_H, [this] { goHistory(-1); });
    key(Qt::Key_X, [this] { goHistory(1); });
    key(Qt::Key_L, [this] { goHistory(1); });
    key(Qt::ALT | Qt::Key_Left, [this] { goHistory(-1); });
    key(Qt::ALT | Qt::Key_Right, [this] { goHistory(1); });
    key(Qt::Key_B, [nextFile] { nextFile(-1); });
    key(Qt::Key_F, [nextFile] { nextFile(1); });
    key(Qt::Key_D, [diffScroll] { diffScroll(18); });
    key(Qt::Key_U, [diffScroll] { diffScroll(-18); });
    key(Qt::Key_G, [this] {
        m_shaEdit->clear();
        m_shaEdit->setFocus();
    });
    key(Qt::Key_Slash, [this] { m_findEdit->setFocus(); });
    key(Qt::Key_Question, [this] { findNext(-1); });
    // Keys that text fields use themselves only act in the list, diff and file panes.
    keyNav(Qt::Key_Left, [this] { goHistory(-1); });
    keyNav(Qt::Key_Right, [this] { goHistory(1); });
    keyNav(Qt::Key_Space, [diffPage] { diffPage(1); });
    keyNav(Qt::Key_Delete, [diffPage] { diffPage(-1); });
    keyNav(Qt::Key_Backspace, [diffPage] { diffPage(-1); });
    keyNav(Qt::Key_Return, [this] { findNext(1); });
    keyNav(Qt::Key_Enter, [this] { findNext(1); });
    keyNav(Qt::SHIFT | Qt::Key_Up, [this] { findNext(-1); });
    keyNav(Qt::SHIFT | Qt::Key_Down, [this] { findNext(1); });
    keyNav(Qt::Key_Home, [this] { selectRow(0); });
    keyNav(Qt::Key_End, [this] { selectRow(m_model->rowCount() - 1); });
    key(Qt::CTRL | Qt::Key_F, [this] { findNext(1); });
    key(Qt::CTRL | Qt::Key_G, [this] { findNext(1); });
    key(Qt::CTRL | Qt::Key_S, [this] { m_diff->searchNext(false); });
    key(Qt::CTRL | Qt::Key_R, [this] { m_diff->searchNext(true); });
    key(Qt::CTRL | Qt::Key_Home, [this] { m_list->scrollToTop(); });
    key(Qt::CTRL | Qt::Key_End, [this] { m_list->scrollToBottom(); });
    key(Qt::CTRL | Qt::Key_Up, [listScroll, this] { listScroll(-m_list->sizeHintForRow(0)); });
    key(Qt::CTRL | Qt::Key_Down, [listScroll, this] { listScroll(m_list->sizeHintForRow(0)); });
    key(Qt::CTRL | Qt::Key_PageUp, [listScroll, this] { listScroll(-m_list->verticalScrollBar()->pageStep()); });
    key(Qt::CTRL | Qt::Key_PageDown, [listScroll, this] { listScroll(m_list->verticalScrollBar()->pageStep()); });
    for (int i = 1; i <= 9; ++i) key(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_0 + i)), [this, i] { goToParent(i); });
}

// ---------------------------------------------------------------------------
// Loading

LogOptions RepoTab::currentOptions(QString *error) {
    LogOptions o = RevArgs::parse(m_views.value(m_currentView).args, &m_repo, error);
    if (!m_prefix.isEmpty() && m_currentView <= 1) {
        for (QString &p : o.paths) p = QDir::cleanPath(m_prefix + QLatin1Char('/') + p);
    }
    o.showLocalChanges = Settings::instance().showLocalChanges;
    return o;
}

void RepoTab::startLoad(bool keepSelection) {
    m_activated = true;
    QString err;
    m_opts = currentOptions(&err);
    if (!err.isEmpty()) emit statusMessage(err, 10000);
    m_refs = m_repo.loadRefs();
    m_model->clear();
    m_model->setRefs(m_refs, Settings::instance().hideRemotes);
    m_pendingSelect.reset();
    if (keepSelection && m_hasSelection) m_pendingSelect = m_selected;
    else if (!m_refs.head.isNull()) m_pendingSelect = m_refs.head;
    if (m_revealId) m_pendingSelect = m_revealId;
    m_selectFirstWhenDone = true;
    m_selected = Oid();
    m_hasSelection = false;
    m_diff->clearAll();
    m_fileModel->setEntries({}, false);
    m_progress->show();
    m_rowLabel->setText(tr("Row"));
    m_loadTimer.start();
    m_loader->start(m_repoPath, m_opts);
}

void RepoTab::onCommitsLoaded(const CommitPtrList &batch) {
    m_model->appendCommits(batch);
    if (!m_findSpec.isEmpty() && m_findSpec.type == FindSpec::Containing) {
        QList<Oid> hits;
        for (const auto &c : batch)
            if (Search::matchesFields(*c, m_findSpec)) hits << c->id;
        m_model->addHighlighted(hits);
    }
    if (m_pendingSelect) {
        int r = m_model->rowOf(*m_pendingSelect);
        if (r >= 0) {
            m_pendingSelect.reset();
            m_revealId.reset();
            m_selectFirstWhenDone = false;
            selectRow(r);
            m_list->scrollTo(m_model->index(r, 0), QAbstractItemView::PositionAtCenter);
        }
    }
    int sel = selectedRow();
    m_rowLabel->setText(sel >= 0 ? tr("Row %1 / %2").arg(sel + 1).arg(m_model->rowCount())
                                 : tr("Row - / %1").arg(m_model->rowCount()));
}

void RepoTab::onLoadFinished(const QString &error) {
    m_progress->hide();
    m_model->finishLoading();
#ifdef __GLIBC__
    malloc_trim(0);   // give the loader thread's freed libgit2 memory back to the system
#endif
    if (qEnvironmentVariableIsSet("GITQ_TIMING"))
        qInfo("loaded %d commits in %lld ms", m_model->rowCount(), qint64(m_loadTimer.elapsed()));
    if (!error.isEmpty()) {
        if (m_model->rowCount() == 0) QMessageBox::warning(this, tr("Error reading commits"), error);
        else emit statusMessage(error, 10000);
    }
    if (m_revealId) {
        emit statusMessage(tr("Commit %1 is not in the current view").arg(m_revealId->shortHex(10)), 10000);
        m_revealId.reset();
        m_pendingLine.reset();
    }
    if (m_selectFirstWhenDone && selectedRow() < 0 && m_model->rowCount() > 0) selectRow(0);
    m_selectFirstWhenDone = false;
    m_pendingSelect.reset();
    if (!m_findSpec.isEmpty() && m_findSpec.type != FindSpec::Containing) runFind();
    int sel = selectedRow();
    m_rowLabel->setText(sel >= 0 ? tr("Row %1 / %2").arg(sel + 1).arg(m_model->rowCount())
                                 : tr("Row - / %1").arg(m_model->rowCount()));
}

void RepoTab::rereadRefs() {
    m_refs = m_repo.loadRefs();
    m_model->setRefs(m_refs, Settings::instance().hideRemotes);
    m_list->viewport()->update();
}

void RepoTab::refreshAfterChange() { startLoad(true); }

void RepoTab::startGitGui() {
    QProcess::startDetached(QStringLiteral("git"), {QStringLiteral("gui")},
                            m_repo.workdir().isEmpty() ? m_repoPath : m_repo.workdir());
}

// ---------------------------------------------------------------------------
// Selection & display

int RepoTab::selectedRow() const {
    QModelIndex i = m_list->currentIndex();
    return i.isValid() ? i.row() : -1;
}

void RepoTab::selectRow(int row) {
    if (row < 0 || row >= m_model->rowCount()) return;
    QModelIndex idx = m_model->index(row, 0);
    m_list->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_list->scrollTo(idx);
}

bool RepoTab::selectCommit(const Oid &id, bool addToHistory) {
    int r = m_model->rowOf(id);
    if (r < 0) return false;
    bool prev = m_navigating;
    m_navigating = !addToHistory;
    selectRow(r);
    m_navigating = prev;
    return true;
}

void RepoTab::revealCommit(const Oid &id, const QString &file, int line) {
    m_pendingLine.reset();
    if (line > 0) m_pendingLine = PendingLine{id, file, line};
    if (m_activated && m_model->rowOf(id) >= 0) {
        m_pendingSelect.reset();
        m_revealId.reset();
        if (m_hasSelection && m_selected == id && !m_rangeDiff) {
            // Already shown: scroll now if its diff is complete.
            if (m_pendingLine && m_diffWatcher.isFinished() && m_diff->scrollToLine(file, line, true))
                m_pendingLine.reset();
            return;
        }
        selectCommit(id);
        m_list->scrollTo(m_model->index(m_model->rowOf(id), 0), QAbstractItemView::PositionAtCenter);
        return;
    }
    if (!m_activated || m_loader->isRunning()) {
        m_revealId = id;
        if (m_activated) m_pendingSelect = id;
        return;
    }
    m_pendingLine.reset();
    emit statusMessage(tr("Commit %1 is not in the current view").arg(id.shortHex(10)), 10000);
}

void RepoTab::moveSelection(int delta) {
    int r = selectedRow();
    selectRow(qBound(0, (r < 0 ? 0 : r + delta), m_model->rowCount() - 1));
}

void RepoTab::onCurrentRowChanged(int row) {
    const CommitInfo *c = m_model->commitAt(row);
    if (!c) return;
    m_rowLabel->setText(tr("Row %1 / %2").arg(row + 1).arg(m_model->rowCount()));
    if (m_hasSelection && c->id == m_selected && !m_rangeDiff) return;
    showCommit(c->id);
}

QString RepoTab::headlineOf(const Oid &id) {
    if (const CommitInfo *c = m_model->commitAt(m_model->rowOf(id))) return c->subject;
    return m_repo.readCommit(id).subject;
}

void RepoTab::showCommit(const Oid &id) {
    const CommitInfo *c = m_model->commitAt(m_model->rowOf(id));
    if (!c) return;
    m_selected = id;
    m_hasSelection = true;
    m_rangeDiff = false;

    // SHA1 field, auto-select / copy to the X11 selection like gitk.
    m_shaEdit->setText(id.hex());
    if (Settings::instance().autoSelect) {
        m_shaEdit->setSelection(0, qBound(4, Settings::instance().autoSelLen, 40));
        if (QApplication::clipboard()->supportsSelection())
            QApplication::clipboard()->setText(id.hex().left(Settings::instance().autoSelLen), QClipboard::Selection);
    }

    if (!m_navigating) {
        if (m_historyIndex < 0 || m_history.value(m_historyIndex) != id) {
            m_history = m_history.mid(0, m_historyIndex + 1);
            m_history.append(id);
            m_historyIndex = int(m_history.size()) - 1;
        }
    }
    m_backBtn->setEnabled(m_historyIndex > 0);
    m_fwdBtn->setEnabled(m_historyIndex + 1 < m_history.size());

    QList<DiffView::Link> parents, children;
    for (const Oid &p : c->parents) parents.append({p, headlineOf(p)});
    for (const Oid &ch : m_model->childrenOf(id)) children.append({ch, headlineOf(ch)});
    m_diff->showHeader(*c, m_model->refsFor(id), parents, children, Settings::instance().dateFormat);
    m_diff->setFindSpec(m_findSpec);

    // Nearby refs in the background.
    if (m_nearbyCancel) m_nearbyCancel->store(true);
    if (Settings::instance().showNearTags && !id.isPseudo()) {
        m_nearbyCancel = std::make_shared<std::atomic<bool>>(false);
        auto cancel = m_nearbyCancel;
        QString path = m_repoPath;
        RefSet refs = m_refs;
        m_nearbyWatcher.setFuture(QtConcurrent::run([path, id, refs, cancel] {
            try {
                Repository repo = Repository::open(path);
                return Nearby::compute(repo, id, refs, cancel.get());
            } catch (const GitError &) {
                return NearbyRefs();
            }
        }));
    }
    m_diffFrom = Oid();
    m_diffTo = id;
    startDiff();
}

void RepoTab::showRangeDiff(const Oid &from, const Oid &to) {
    m_rangeDiff = true;
    m_rangeValid = true;
    m_diffFrom = from;
    m_diffTo = to;
    auto label = [&](const Oid &id) {
        return id.isPseudo() ? (id.isWorkdir() ? tr("local changes") : tr("index"))
                             : QStringLiteral("%1 (%2)").arg(id.shortHex(10), headlineOf(id));
    };
    m_rangeHeader = tr("Diff from %1\n       to %2").arg(label(from), label(to));
    m_diff->showRangeHeader(m_rangeHeader);
    startDiff();
}

DiffOptions RepoTab::diffOptions() const {
    DiffOptions o;
    o.context = m_contextSpin->value();
    o.ignoreSpaceChange = m_ignoreSpace->isChecked();
    if (Settings::instance().limitDiffs) o.paths = m_opts.paths;
    o.submodules = m_submoduleMode->currentIndex();
    o.expandSubmodules = QStringList(m_expandedSubmodules.cbegin(), m_expandedSubmodules.cend());
    o.expandSubmodules.sort();
    return o;
}

void RepoTab::startDiff() {
    if (m_rangeDiff ? !m_rangeValid : (!m_hasSelection || m_model->rowOf(m_selected) < 0)) return;
    const quint64 seq = ++m_diffSeq;
    const QString path = m_repoPath;
    const DiffOptions opts = diffOptions();
    const bool range = m_rangeDiff;
    const Oid from = m_diffFrom, to = m_rangeDiff ? m_diffTo : m_selected;
    const bool tree = m_treeMode->isChecked();
    m_diffWatcher.setFuture(QtConcurrent::run([=] {
        DiffJob job;
        job.seq = seq;
        try {
            Repository repo = Repository::open(path);
            job.diff = range ? DiffEngine::diffBetween(repo, from, to, opts) : DiffEngine::diffCommit(repo, to, opts);
            if (tree) job.tree = DiffEngine::treeFiles(repo, to, opts.submodules != DiffOptions::SubmoduleSummary);
        } catch (const GitError &e) {
            job.diff.error = e.message();
        }
        return job;
    }));
}

void RepoTab::onDiffReady() {
    DiffJob job = m_diffWatcher.result();
    if (job.seq != m_diffSeq) return;
    m_lastDiff = job.diff;
    // Re-render the header + diff (the header may already be present for this commit).
    if (!m_rangeDiff) {
        const CommitInfo *c = m_model->commitAt(m_model->rowOf(m_selected));
        if (!c) return;
        QList<DiffView::Link> parents, children;
        for (const Oid &p : c->parents) parents.append({p, headlineOf(p)});
        for (const Oid &ch : m_model->childrenOf(m_selected)) children.append({ch, headlineOf(ch)});
        int keep = m_diff->verticalScrollBar()->value();
        m_diff->showHeader(*c, m_model->refsFor(m_selected), parents, children, Settings::instance().dateFormat);
        if (m_nearbyWatcher.isFinished() && m_nearbyWatcher.future().isValid()) {
            NearbyRefs n = m_nearbyWatcher.result();
            if (n.id == m_selected) m_diff->showNearby(n, Settings::instance().maxRefs);
        }
        m_diff->verticalScrollBar()->setValue(keep);
    } else {
        // Redisplay (options changed): start from the header again.
        int keep = m_diff->verticalScrollBar()->value();
        m_diff->showRangeHeader(m_rangeHeader);
        m_diff->verticalScrollBar()->setValue(keep);
    }
    m_diff->setSubmoduleToggles(m_submoduleMode->currentIndex() == DiffOptions::SubmoduleLog);
    m_diff->setMode(m_modeOld->isChecked() ? DiffView::ShowOld : m_modeNew->isChecked() ? DiffView::ShowNew : DiffView::ShowDiff);
    m_diff->setWordMode(DiffView::WordMode(m_wordDiff->currentIndex()));
    m_diff->appendDiff(job.diff);
    m_diff->setFindSpec(m_findSpec);
    if (m_pendingLine && !m_rangeDiff && m_pendingLine->commit == m_selected) {
        m_diff->scrollToLine(m_pendingLine->path, m_pendingLine->line, true);
        m_pendingLine.reset();
    }
    if (m_treeMode->isChecked()) {
        QList<FileListModel::Entry> entries;
        for (const QString &p : job.tree) entries.append({p, QChar()});
        m_fileModel->setEntries(entries, false);
    } else {
        fillFileList();
    }
}

void RepoTab::fillFileList() {
    if (m_treeMode->isChecked()) {
        if (m_hasSelection) startDiff();
        return;
    }
    QList<FileListModel::Entry> entries;
    for (const FileDiff &f : m_lastDiff.files) entries.append({f.path(), f.status});
    m_fileModel->setEntries(entries, true);
    QSet<QString> hl;
    if (m_findSpec.type == FindSpec::TouchingPaths && !m_findSpec.text.isEmpty()) {
        PathFilter pf(m_repo.raw(), m_findSpec.text.split(QLatin1Char(' '), Qt::SkipEmptyParts));
        for (const auto &e : entries)
            if (pf.matchesPath(e.path)) hl.insert(e.path);
    }
    m_fileModel->setHighlightedPaths(hl);
}

void RepoTab::onFileClicked(const QModelIndex &idx) {
    QString path = idx.data(FileListModel::PathRole).toString();
    if (m_treeMode->isChecked()) {
        if (path.isEmpty()) return;
        QByteArray data = DiffEngine::fileContents(m_repo, m_selected, path);
        m_diff->showFileContents(path, QString::fromUtf8(data));
        return;
    }
    if (path.isEmpty()) m_diff->scrollToTop();
    else m_diff->scrollToFile(path);
}

void RepoTab::goHistory(int delta) {
    int i = m_historyIndex + delta;
    if (i < 0 || i >= m_history.size()) return;
    m_historyIndex = i;
    if (!selectCommit(m_history[i], false)) emit statusMessage(tr("Commit is no longer in the view"), 5000);
    m_backBtn->setEnabled(m_historyIndex > 0);
    m_fwdBtn->setEnabled(m_historyIndex + 1 < m_history.size());
}

void RepoTab::goToParent(int n) {
    const CommitInfo *c = m_model->commitAt(selectedRow());
    if (!c || n > c->parents.size()) return;
    selectCommit(c->parents[n - 1]);
}

void RepoTab::gotoCommitFromField() {
    QString text = m_shaEdit->text().trimmed();
    if (text.isEmpty()) return;
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{4,40}$"));
    Oid id;
    if (hex.match(text).hasMatch()) {
        QList<int> rows = m_model->rowsWithPrefix(text);
        if (rows.size() == 1) {
            selectRow(rows.first());
            return;
        }
        if (rows.size() > 1) {
            QMessageBox::warning(this, tr("Error"), tr("Short SHA1 id %1 is ambiguous").arg(text));
            return;
        }
    }
    QString err;
    id = m_repo.resolveCommit(text, &err);
    if (id.isNull()) {
        QMessageBox::warning(this, tr("Error"), tr("Revision %1 is not known").arg(text));
        return;
    }
    if (!selectCommit(id))
        QMessageBox::warning(this, tr("Error"), tr("Revision %1 is not in the current view").arg(text));
}

// ---------------------------------------------------------------------------
// Find

FindSpec RepoTab::currentFindSpec() const {
    FindSpec s;
    s.type = FindSpec::Type(m_findType->currentIndex());
    s.mode = FindSpec::Mode(m_findMode->currentIndex());
    s.field = FindSpec::Field(m_findField->currentIndex());
    s.text = m_findEdit->text();
    return s;
}

void RepoTab::findChanged() {
    FindSpec s = currentFindSpec();
    if (s.type == FindSpec::Containing) runFind();
    else m_findTimer->start();
}

void RepoTab::runFind() {
    m_findSpec = currentFindSpec();
    m_searcher->cancel();
    m_model->setHighlighted({});
    m_diff->setFindSpec(m_findSpec);
    if (m_findSpec.isEmpty()) return;
    if (m_findSpec.type == FindSpec::Containing) {
        QSet<Oid> hits;
        for (int r = 0; r < m_model->rowCount(); ++r) {
            const CommitInfo *c = m_model->commitAt(r);
            if (Search::matchesFields(*c, m_findSpec)) hits.insert(c->id);
        }
        m_model->setHighlighted(hits);
    } else {
        m_contentSearchDone = false;
        m_searcher->start(m_repoPath, m_model->allIds(), m_findSpec);
    }
    if (!m_patchMode->isChecked()) return;
    fillFileList();
}

void RepoTab::findNext(int dir) {
    if (m_findEdit->text().isEmpty()) {
        m_findEdit->setFocus();
        return;
    }
    if (!(currentFindSpec() == m_findSpec)) runFind();
    const int n = m_model->rowCount();
    int r = selectedRow();
    if (r < 0) r = dir > 0 ? -1 : n;
    for (int i = r + dir; i >= 0 && i < n; i += dir) {
        const CommitInfo *c = m_model->commitAt(i);
        if (m_model->isHighlighted(c->id)) {
            selectRow(i);
            return;
        }
    }
    emit statusMessage(m_contentSearchDone ? tr("Not found") : tr("Not found yet (still searching)"), 4000);
}

// ---------------------------------------------------------------------------
// Views

void RepoTab::showView(int index) {
    if (index < 0 || index >= m_views.size()) return;
    m_currentView = index;
    emit viewsChanged();
    startLoad(true);
}

void RepoTab::newView() {
    ViewDef v;
    v.name = tr("View %1").arg(m_views.size());
    ViewDialog dlg(v, true, this);
    int rc = dlg.exec();
    if (rc == QDialog::Rejected) return;
    m_views.append(dlg.view());
    m_currentView = int(m_views.size()) - 1;
    Settings::instance().views.clear();
    for (const ViewDef &d : m_views.mid(1))
        if (d.remember) Settings::instance().views << d;
    Settings::instance().save();
    emit viewsChanged();
    startLoad(true);
}

void RepoTab::editView() {
    if (m_currentView < 0 || m_currentView >= m_views.size()) return;
    ViewDialog dlg(m_views[m_currentView], false, this);
    int rc = dlg.exec();
    if (rc == QDialog::Rejected) return;
    ViewDef v = dlg.view();
    if (m_currentView == 0) v.name = m_views[0].name;
    bool changed = !(v.args == m_views[m_currentView].args);
    m_views[m_currentView] = v;
    Settings::instance().views.clear();
    for (const ViewDef &d : m_views.mid(1))
        if (d.remember && d.name != tr("Command line")) Settings::instance().views << d;
    Settings::instance().save();
    emit viewsChanged();
    if (changed) startLoad(true);
}

void RepoTab::deleteView() {
    if (m_currentView <= 0) return;
    m_views.removeAt(m_currentView);
    m_currentView = 0;
    Settings::instance().views.clear();
    for (const ViewDef &d : m_views.mid(1))
        if (d.remember) Settings::instance().views << d;
    Settings::instance().save();
    emit viewsChanged();
    startLoad(true);
}

// ---------------------------------------------------------------------------
// Context menus

void RepoTab::rowMenu(int row, const QPoint &pos) {
    const CommitInfo *c = m_model->commitAt(row);
    if (!c) return;
    const Oid id = c->id;
    const bool hasSel = m_hasSelection;
    const Oid sel = m_selected;
    const Oid mark = m_model->marked();
    QMenu menu(this);
    auto diffSel = [this, id, sel, hasSel](bool reverse) {
        if (!hasSel || sel == id) return;
        reverse ? showRangeDiff(sel, id) : showRangeDiff(id, sel);
    };
    auto diffMark = [this, id, mark](bool reverse) {
        if (mark.isNull()) return;
        reverse ? showRangeDiff(mark, id) : showRangeDiff(id, mark);
    };
    QAction *a;
    a = menu.addAction(tr("Diff this -> selected"), this, [diffSel] { diffSel(false); });
    a->setEnabled(hasSel && sel != id);
    a = menu.addAction(tr("Diff selected -> this"), this, [diffSel] { diffSel(true); });
    a->setEnabled(hasSel && sel != id);
    menu.addAction(tr("Make patch"), this, [this, id] { makePatch(id); });
    if (!id.isPseudo()) {
        menu.addAction(tr("Create tag"), this, [this, id] { createTag(id); });
        menu.addAction(tr("Copy commit reference"), this, [this, id] { copyReference(id); });
        menu.addAction(tr("Write commit to file"), this, [this, id] { writeCommit(id); });
        menu.addAction(tr("Create new branch"), this, [this, id] { createBranch(id); });
        menu.addAction(tr("Cherry-pick this commit"), this, [this, id] { cherryPick(id); });
        menu.addAction(tr("Reset HEAD branch to here"), this, [this, id] { resetHere(id); });
        menu.addAction(tr("Mark this commit"), this, [this, id] { m_model->setMarked(id); });
        a = menu.addAction(tr("Return to mark"), this, [this, mark] { selectCommit(mark); });
        a->setEnabled(!mark.isNull());
        a = menu.addAction(tr("Find descendant of this and mark"), this, [this, id] { findCommonDescendant(id); });
        a->setEnabled(!mark.isNull());
        a = menu.addAction(tr("Compare with marked commit"), this, [this, id] { compareWithMarked(id); });
        a->setEnabled(!mark.isNull());
    }
    a = menu.addAction(tr("Diff this -> marked commit"), this, [diffMark] { diffMark(false); });
    a->setEnabled(!mark.isNull() && mark != id);
    a = menu.addAction(tr("Diff marked commit -> this"), this, [diffMark] { diffMark(true); });
    a->setEnabled(!mark.isNull() && mark != id);
    if (!id.isPseudo()) {
        menu.addAction(tr("Revert this commit"), this, [this, id] { revertCommit(id); });
        menu.addSeparator();
        menu.addAction(tr("Check out this commit (detached)"), this, [this, id] {
            reportOp(Ops::checkoutCommit(m_repo, id), tr("Checkout"));
        });
        menu.addAction(tr("Copy commit ID"), this, [id] { QApplication::clipboard()->setText(id.hex()); });
    }
    menu.exec(pos);
}

void RepoTab::refMenu(int, const RefInfo &ref, const QPoint &pos) {
    QMenu menu(this);
    menu.addAction(tr("Check out this branch"), this, [this, ref] { checkoutBranch(ref); });
    menu.addAction(tr("Rename this branch"), this, [this, ref] { renameBranch(ref); });
    menu.addAction(tr("Remove this branch"), this, [this, ref] { removeBranch(ref); });
    menu.addAction(tr("Copy branch name"), this, [ref] { QApplication::clipboard()->setText(ref.name); });
    menu.exec(pos);
}

void RepoTab::fileMenu(const QPoint &pos) {
    QModelIndex idx = m_files->indexAt(pos);
    QString path = idx.data(FileListModel::PathRole).toString();
    if (path.isEmpty()) return;
    QMenu menu(this);
    auto highlight = [this, path](bool only) {
        QString text = only ? path : (m_findType->currentIndex() == 1 && !m_findEdit->text().isEmpty()
                                          ? m_findEdit->text() + QLatin1Char(' ') + path
                                          : path);
        m_findType->setCurrentIndex(1);
        m_findEdit->setText(text);
        runFind();
    };
    menu.addAction(tr("Highlight this too"), this, [highlight] { highlight(false); });
    menu.addAction(tr("Highlight this only"), this, [highlight] { highlight(true); });
    menu.addAction(tr("External diff"), this, [this, path] { externalDiff(path); });
    std::optional<Oid> parent = sideCommit(true);
    QAction *a = menu.addAction(tr("Blame parent commit"), this, [this, path] { blame(path, true, 0); });
    a->setEnabled(parent && !m_selected.isPseudo());
    menu.addAction(tr("Blame this commit"), this, [this, path] { blame(path, false, 0); });
    if (canOpenSubmodule(path))
        menu.addAction(tr("Open submodule in new tab"), this, [this, path] { openSubmodule(path, Oid()); });
    menu.addAction(tr("Copy path"), this, [path] { QApplication::clipboard()->setText(path); });
    menu.exec(m_files->viewport()->mapToGlobal(pos));
}

void RepoTab::diffMenu(const DiffLineInfo &info, const QPoint &pos) {
    QMenu menu(this);
    bool line = !info.path.isEmpty() && !info.kind.isNull() && !m_rangeDiff;
    QAction *a = menu.addAction(tr("Show origin of this line"), this, [this, info] { showLineOrigin(info); });
    a->setEnabled(line);
    a = menu.addAction(tr("Run blame on this line"), this, [this, info] {
        const bool old = info.kind == QLatin1Char('-');
        blame(info.path, old, old ? info.oldLine : info.newLine);
    });
    a->setEnabled(line);
    if (!info.path.isEmpty() && canOpenSubmodule(info.path))
        menu.addAction(tr("Open submodule in new tab"), this, [this, info] { openSubmodule(info.path, Oid()); });
    menu.addSeparator();
    menu.addAction(tr("Copy"), m_diff, &QPlainTextEdit::copy);
    menu.addAction(tr("Select all"), m_diff, &QPlainTextEdit::selectAll);
    menu.exec(pos);
}

// ---------------------------------------------------------------------------
// Commands

void RepoTab::reportOp(const OpResult &r, const QString &what) {
    if (!r.ok) QMessageBox::warning(this, tr("%1 failed").arg(what), r.message);
    else if (!r.message.isEmpty()) emit statusMessage(r.message, 5000);
    refreshAfterChange();
    if (r.ok && !r.newCommit.isNull()) m_pendingSelect = r.newCommit;
}

void RepoTab::makePatch(const Oid &id) {
    Oid from = m_selected;
    if (!m_hasSelection || from == id) {
        const CommitInfo *c = m_model->commitAt(m_model->rowOf(id));
        from = c && !c->parents.isEmpty() ? c->parents.first() : Oid();
    }
    PatchDialog dlg(tr("Gitq: make patch"), from.hex(), id.hex(), QDir::current().filePath(QStringLiteral("patch")), true, this);
    if (dlg.exec() != QDialog::Accepted) return;
    Oid f = m_repo.resolveCommit(dlg.from()), t = m_repo.resolveCommit(dlg.to());
    if (dlg.from() == Oid::workdirId().hex()) f = Oid::workdirId();
    if (dlg.to() == Oid::workdirId().hex()) t = Oid::workdirId();
    if (dlg.from() == Oid::indexId().hex()) f = Oid::indexId();
    if (dlg.to() == Oid::indexId().hex()) t = Oid::indexId();
    QString err;
    QString text = Ops::makePatch(m_repo, f, t, &err);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("Error creating patch"), err);
        return;
    }
    QFile file(dlg.file());
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0)
        QMessageBox::warning(this, tr("Error creating patch"), file.errorString());
}

void RepoTab::writeCommit(const Oid &id) {
    PatchDialog dlg(tr("Gitq: write commit to file"), {}, id.hex(), QDir::current().filePath(QStringLiteral("commit-") + id.shortHex()),
                    false, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    QString text = Ops::commitAsEmail(m_repo, id, &err);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("Error writing commit"), err);
        return;
    }
    QFile file(dlg.file());
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0)
        QMessageBox::warning(this, tr("Error writing commit"), file.errorString());
}

void RepoTab::createTag(const Oid &id) {
    NameDialog dlg(tr("Gitq: create tag"), id.hex(), tr("Tag name:"), true, this);
    if (dlg.exec() != QDialog::Accepted || dlg.name().isEmpty()) return;
    for (const RefInfo &r : m_refs.refs)
        if (r.kind == RefInfo::Tag && r.name == dlg.name()) {
            QMessageBox::warning(this, tr("Error"), tr("Tag \"%1\" already exists").arg(dlg.name()));
            return;
        }
    OpResult r = Ops::createTag(m_repo, dlg.name(), id, dlg.message());
    if (!r.ok) QMessageBox::warning(this, tr("Error creating tag"), r.message);
    rereadRefs();
}

void RepoTab::createBranch(const Oid &id) {
    NameDialog dlg(tr("Gitq: create new branch"), id.hex(), tr("Name:"), false, this);
    if (dlg.exec() != QDialog::Accepted || dlg.name().isEmpty()) return;
    bool exists = false;
    for (const RefInfo &r : m_refs.refs) exists |= r.kind == RefInfo::LocalBranch && r.name == dlg.name();
    if (exists && QMessageBox::question(this, tr("Confirm overwrite"),
                                        tr("Branch '%1' already exists. Overwrite?").arg(dlg.name())) != QMessageBox::Yes)
        return;
    OpResult r = Ops::createBranch(m_repo, dlg.name(), id, exists);
    if (!r.ok) QMessageBox::warning(this, tr("Error creating branch"), r.message);
    rereadRefs();
}

void RepoTab::cherryPick(const Oid &id) {
    if (!m_refs.head.isNull() && m_repo.isAncestor(id, m_refs.head) &&
        QMessageBox::question(this, tr("Confirm cherry-pick"),
                              tr("Commit %1 is already included in branch %2 -- really re-apply it?")
                                  .arg(id.shortHex(8), m_refs.headBranch)) != QMessageBox::Yes)
        return;
    int mainline = 0;
    const CommitInfo *c = m_model->commitAt(m_model->rowOf(id));
    if (c && c->parents.size() > 1) {
        bool ok = false;
        mainline = QInputDialog::getInt(this, tr("Cherry-pick merge"),
                                        tr("The commit is a merge. Parent number to use as mainline:"), 1, 1,
                                        int(c->parents.size()), 1, &ok);
        if (!ok) return;
    }
    emit statusMessage(tr("Cherry-picking..."), 0);
    OpResult r = Ops::cherryPick(m_repo, id, mainline);
    if (r.conflicts)
        QMessageBox::warning(this, tr("Cherry-pick conflicts"),
                             tr("Cherry-pick of %1 resulted in conflicts. Resolve them in the work tree and commit "
                                "with git (note: commit hooks are not run by gitq).")
                                 .arg(id.shortHex(8)));
    reportOp(r.conflicts ? OpResult{true, false, {}, {}} : r, tr("Cherry-pick"));
}

void RepoTab::revertCommit(const Oid &id) {
    if (QMessageBox::question(this, tr("Confirm revert"), tr("Revert commit %1 (%2)?").arg(id.shortHex(8), headlineOf(id))) !=
        QMessageBox::Yes)
        return;
    int mainline = 0;
    const CommitInfo *c = m_model->commitAt(m_model->rowOf(id));
    if (c && c->parents.size() > 1) {
        bool ok = false;
        mainline = QInputDialog::getInt(this, tr("Revert merge"), tr("Parent number to use as mainline:"), 1, 1,
                                        int(c->parents.size()), 1, &ok);
        if (!ok) return;
    }
    OpResult r = Ops::revert(m_repo, id, mainline);
    if (r.conflicts)
        QMessageBox::warning(this, tr("Revert conflicts"),
                             tr("Revert resulted in conflicts. Resolve them in the work tree and commit with git."));
    reportOp(r.conflicts ? OpResult{true, false, {}, {}} : r, tr("Revert"));
}

void RepoTab::resetHere(const Oid &id) {
    ResetDialog dlg(m_refs.headBranch.isEmpty() ? QStringLiteral("HEAD") : m_refs.headBranch, id.shortHex(10), this);
    if (dlg.exec() != QDialog::Accepted) return;
    reportOp(Ops::reset(m_repo, id, dlg.type()), tr("Reset"));
}

void RepoTab::findCommonDescendant(const Oid &id) {
    Oid mark = m_model->marked();
    if (mark.isNull()) return;
    int start = qMin(m_model->rowOf(id), m_model->rowOf(mark));
    for (int r = start; r >= 0; --r) {
        const CommitInfo *c = m_model->commitAt(r);
        if (c->id.isPseudo()) continue;
        if (m_repo.isAncestor(id, c->id) && m_repo.isAncestor(mark, c->id)) {
            selectRow(r);
            m_model->setMarked(c->id);
            return;
        }
    }
    QMessageBox::information(this, tr("Find descendant"), tr("No common descendant found in the view"));
}

void RepoTab::compareWithMarked(const Oid &id) {
    Oid mark = m_model->marked();
    if (mark.isNull()) return;
    // Commits reachable from one side only, compared by patch id.
    auto sideOnly = [&](const Oid &a, const Oid &b) {
        QList<Oid> out;
        git_revwalk *w = nullptr;
        if (git_revwalk_new(&w, m_repo.raw()) < 0) return out;
        RevwalkPtr walk(w);
        git_revwalk_sorting(w, GIT_SORT_TOPOLOGICAL);
        git_revwalk_push(w, a.ptr());
        git_revwalk_hide(w, b.ptr());
        git_oid o;
        int n = 0;
        while (git_revwalk_next(&o, w) == 0 && n++ < 5000) out << Oid(o);
        git_error_clear();
        return out;
    };
    auto patchId = [&](const Oid &c) -> QString {
        CommitPtr cm = m_repo.lookupCommit(c);
        if (!cm || git_commit_parentcount(cm.get()) != 1) return QString();
        git_tree *t = nullptr, *pt = nullptr;
        git_commit *p = nullptr;
        git_commit_tree(&t, cm.get());
        git_commit_parent(&p, cm.get(), 0);
        if (p) git_commit_tree(&pt, p);
        git_diff *d = nullptr;
        QString res;
        if (git_diff_tree_to_tree(&d, m_repo.raw(), pt, t, nullptr) == 0) {
            git_oid pid;
            if (git_diff_patchid(&pid, d, nullptr) == 0) res = Oid(pid).hex();
            git_diff_free(d);
        }
        git_tree_free(t);
        git_tree_free(pt);
        git_commit_free(p);
        git_error_clear();
        return res;
    };
    QList<Oid> mine = sideOnly(id, mark), theirs = sideOnly(mark, id);
    QHash<QString, Oid> theirIds;
    for (const Oid &o : theirs) {
        QString p = patchId(o);
        if (!p.isEmpty()) theirIds.insert(p, o);
    }
    QStringList lines;
    lines << tr("Comparing %1 (this) with %2 (marked)").arg(id.shortHex(8), mark.shortHex(8)) << QString();
    lines << tr("Commits on this side only (%1):").arg(mine.size());
    for (const Oid &o : mine) {
        QString p = patchId(o);
        auto it = theirIds.constFind(p);
        lines << QStringLiteral("  %1 %2%3").arg(o.shortHex(8), headlineOf(o),
                                                p.isEmpty() || it == theirIds.cend()
                                                    ? QString()
                                                    : tr("   [same patch as %1]").arg(it->shortHex(8)));
    }
    lines << QString() << tr("Commits on the marked side only (%1):").arg(theirs.size());
    for (const Oid &o : theirs) lines << QStringLiteral("  %1 %2").arg(o.shortHex(8), headlineOf(o));
    m_rangeDiff = true;
    m_rangeValid = false;   // text-only report: nothing to re-diff when options change
    m_diff->showRangeHeader(lines.join(QLatin1Char('\n')));
}

void RepoTab::copyReference(const Oid &id) {
    const CommitInfo *c = m_model->commitAt(m_model->rowOf(id));
    if (!c) return;
    QString ref = QStringLiteral("%1 (%2, %3)").arg(id.shortHex(7), c->subject,
                                                     QDateTime::fromSecsSinceEpoch(c->author.time).toString(QStringLiteral("yyyy-MM-dd")));
    QApplication::clipboard()->setText(ref);
    if (QApplication::clipboard()->supportsSelection()) QApplication::clipboard()->setText(ref, QClipboard::Selection);
}

void RepoTab::checkoutBranch(const RefInfo &ref) {
    reportOp(Ops::checkoutBranch(m_repo, ref.name), tr("Checkout"));
}

void RepoTab::renameBranch(const RefInfo &ref) {
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Rename branch"), tr("New name for branch %1:").arg(ref.name),
                                         QLineEdit::Normal, ref.name, &ok);
    if (!ok || name.isEmpty() || name == ref.name) return;
    OpResult r = Ops::renameBranch(m_repo, ref.name, name, false);
    if (!r.ok) QMessageBox::warning(this, tr("Error renaming branch"), r.message);
    rereadRefs();
}

void RepoTab::removeBranch(const RefInfo &ref) {
    if (ref.name == m_refs.headBranch) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot delete the currently checked-out branch"));
        return;
    }
    bool merged = false;
    for (const RefInfo &other : m_refs.refs)
        if (other.fullName != ref.fullName && other.kind != RefInfo::Tag && m_repo.isAncestor(ref.commit, other.commit))
            merged = true;
    QString q = merged ? tr("Really remove branch %1?").arg(ref.name)
                       : tr("The commits on branch %1 aren't on any other branch.\nReally delete branch %1?").arg(ref.name);
    if (QMessageBox::question(this, tr("Confirm"), q) != QMessageBox::Yes) return;
    OpResult r = Ops::deleteBranch(m_repo, ref.name);
    if (!r.ok) QMessageBox::warning(this, tr("Error removing branch"), r.message);
    rereadRefs();
}

void RepoTab::externalDiff(const QString &path) {
    Oid newId = m_rangeDiff ? m_diffTo : m_selected;
    Oid oldId = m_rangeDiff ? m_diffFrom : Oid();
    if (!m_rangeDiff) {
        if (newId.isWorkdir()) oldId = Oid::indexId();
        else if (newId.isIndex()) oldId = m_refs.head;
        else {
            const CommitInfo *c = m_model->commitAt(m_model->rowOf(newId));
            if (c && !c->parents.isEmpty()) oldId = c->parents.first();
        }
    }
    auto dir = std::make_shared<QTemporaryDir>();
    if (!dir->isValid()) return;
    m_tempDirs << dir;
    auto write = [&](const Oid &id, const QString &tag) -> QString {
        if (id.isWorkdir()) return QDir(m_repo.workdir()).filePath(path);
        QString name = dir->filePath(tag + QStringLiteral("-") + QFileInfo(path).fileName());
        QFile f(name);
        if (f.open(QIODevice::WriteOnly)) f.write(oldId.isNull() && tag == QLatin1String("old") ? QByteArray()
                                                                                             : DiffEngine::fileContents(m_repo, id, path));
        return name;
    };
    QString a = write(oldId, QStringLiteral("old")), b = write(newId, QStringLiteral("new"));
    QStringList cmd = QProcess::splitCommand(Settings::instance().extDiffTool);
    if (cmd.isEmpty()) return;
    QString prog = cmd.takeFirst();
    if (!QProcess::startDetached(prog, cmd << a << b))
        QMessageBox::warning(this, tr("External diff"), tr("Could not start %1").arg(prog));
}

std::optional<Oid> RepoTab::sideCommit(bool oldSide) const {
    if (!oldSide) return m_selected;
    const CommitInfo *c = m_model->commitAt(m_model->rowOf(m_selected));
    if (!c || c->parents.isEmpty()) return std::nullopt;
    return c->parents.first();
}

void RepoTab::blame(const QString &path, bool oldSide, int line) {
    std::optional<Oid> at = sideCommit(oldSide);
    if (!at) return;
    QString repoPath = m_repoPath, file = path;
    Oid commit = *at;
    SubmoduleLocation loc;
    const bool inSubmodule = DiffEngine::locateInSubmodule(m_repo, commit, path, &loc) && !loc.path.isEmpty();
    if (inSubmodule) {
        repoPath = loc.repoPath;
        file = loc.path;
        commit = loc.commit;   // work tree: blame its HEAD (null), like the superproject's
    }
    auto *w = new BlameWindow(repoPath, file, commit, line, this);
    connect(w, &BlameWindow::commitActivated, this, [this, inSubmodule, repoPath](const Oid &id) {
        if (inSubmodule) {
            emit openRepositoryRequested(repoPath, id, {}, 0);
            return;
        }
        if (!selectCommit(id)) emit statusMessage(tr("Commit %1 is not in the current view").arg(id.shortHex(10)), 5000);
        emit raiseRequested();
    });
    w->show();
}

bool RepoTab::canOpenSubmodule(const QString &path) const {
    if (const SubmoduleChange *s = m_lastDiff.submoduleFor(path); s && !s->repoPath.isEmpty()) return true;
    SubmoduleLocation loc;
    return m_hasSelection && DiffEngine::locateInSubmodule(m_repo, m_selected, path, &loc);
}

void RepoTab::openSubmodule(const QString &path, const Oid &commit) {
    QString repoPath;
    Oid at = commit;
    if (const SubmoduleChange *s = m_lastDiff.submoduleFor(path); s && !s->repoPath.isEmpty()) {
        repoPath = s->repoPath;
        if (at.isNull()) at = s->newId.isNull() ? s->oldId : s->newId;
        if (s->newIsWorkdir && commit.isNull()) at = Oid();   // the submodule's own local changes
    } else if (SubmoduleLocation loc; m_hasSelection && DiffEngine::locateInSubmodule(m_repo, m_selected, path, &loc)) {
        repoPath = loc.repoPath;
        if (at.isNull()) at = loc.commit;
    }
    if (repoPath.isEmpty()) {
        emit statusMessage(tr("The submodule's repository is not available (not checked out)"), 5000);
        return;
    }
    emit openRepositoryRequested(repoPath, at, {}, 0);
}

void RepoTab::showLineOrigin(const DiffLineInfo &info) {
    const bool old = info.kind == QLatin1Char('-');
    std::optional<Oid> side = sideCommit(old);
    if (!side) return;
    Oid at = *side;
    QString path = info.path;
    const int line = old ? info.oldLine : info.newLine;
    if (line <= 0) return;
    SubmoduleLocation loc;
    const bool inSubmodule = DiffEngine::locateInSubmodule(m_repo, at, path, &loc) && !loc.path.isEmpty();
    BlameResult b;
    if (inSubmodule) {
        try {
            b = Blame::blameFile(Repository::open(loc.repoPath), loc.path, loc.commit);
        } catch (const GitError &e) {
            b.error = e.message();
        }
    } else {
        b = Blame::blameFile(m_repo, path, at);
    }
    if (line > b.lines.size()) {
        QMessageBox::warning(this, tr("Error"), b.error.isEmpty() ? tr("Couldn't find the origin of this line") : b.error);
        return;
    }
    const BlameLine &bl = b.lines[line - 1];
    if (inSubmodule) {
        emit openRepositoryRequested(loc.repoPath, bl.commit, bl.origPath, bl.origLine);
        return;
    }
    if (m_model->rowOf(bl.commit) >= 0) {
        revealCommit(bl.commit, bl.origPath, bl.origLine);
    } else {
        QMessageBox::information(this, tr("Line origin"),
                                 tr("Line originates in commit %1 (%2), which is not in the current view.")
                                     .arg(bl.commit.shortHex(10), bl.summary));
    }
}

void RepoTab::showRefsDialog() {
    auto *dlg = new RefsDialog(m_refs.refs, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &RefsDialog::refChosen, this, [this](const Oid &id) {
        if (!selectCommit(id)) emit statusMessage(tr("That commit is not in the current view"), 5000);
    });
    dlg->show();
}

void RepoTab::applySettings() {
    const Settings &st = Settings::instance();
    m_list->setFont(st.mainFont);
    m_diff->applyFonts();
    m_model->setDateFormat(st.dateFormat);
    m_model->setRefs(m_refs, st.hideRemotes);
    QPalette pal = m_list->palette();
    pal.setColor(QPalette::Base, st.bgColor);
    pal.setColor(QPalette::Text, st.fgColor);
    m_list->setPalette(pal);
    // Multi-line rows show author and date under the headline: drop those columns and the header
    // (QHeaderView keeps the hidden sections' sizes for switching back). Applied every time, as a
    // header state restored from the settings may come from the other mode.
    const bool multi = st.commitListMultiline;
    m_list->header()->setSectionHidden(CommitModel::AuthorColumn, multi);
    m_list->header()->setSectionHidden(CommitModel::DateColumn, multi);
    m_list->header()->setVisible(!multi);
    if (m_delegate->multiline() != multi) {
        m_delegate->setMultiline(multi);
        m_list->doItemsLayout();   // row height changed
        if (m_list->currentIndex().isValid()) m_list->scrollTo(m_list->currentIndex(), QAbstractItemView::PositionAtCenter);
    }
    m_list->viewport()->update();
}

void RepoTab::settingsChanged(bool reload) {
    applySettings();
    if (reload) {
        if (m_activated) startLoad(true);
    } else if (m_hasSelection) {
        showCommit(m_selected);
    }
}

} // namespace gitq
