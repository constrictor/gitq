// The main window: one tab per open repository above a shared dockable pane layout, or a welcome
// page while no repository is open. The open tabs are remembered between runs.
#pragma once

#include "git/Oid.h"

#include <QMainWindow>
#include <QStringList>

class QAction;
class QActionGroup;
class QMenu;
class QStackedWidget;
class QTabBar;

namespace ads {
class CDockManager;
class CDockWidget;
} // namespace ads

namespace gitq {

class RepoTab;
class WelcomePage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    // A window with just this repository open. Throws GitError if it can't be opened.
    MainWindow(const QString &repoPath, const QStringList &args, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Reopens the tabs that were open when gitq last saved its session.
    void restoreSession();
    // Opens the repository containing `path` in a new tab, or switches to its tab if it is
    // already open (reopening it with `args` if those differ). `prefix` is the directory relative
    // paths in `args` are relative to. Throws GitError if there is no repository there.
    RepoTab *openRepository(const QString &path, const QStringList &args = {}, const QString &prefix = {},
                            bool makeCurrent = true);
    // Opens the repository at `path` (in a new tab showing all branches, unless it is open
    // already) and reveals `commit` there, scrolled to `line` of `file` if given. Reports failure
    // in the status bar.
    RepoTab *openRepositoryAt(const QString &path, const Oid &commit, const QString &file = {}, int line = 0);
    void closeTab(int index);

    int tabCount() const { return int(m_tabs.size()); }
    RepoTab *tab(int index) const { return m_tabs.value(index); }
    RepoTab *currentTab() const;
    void setCurrentTab(int index);

    // Recently opened repositories, most recent first.
    static QStringList recentRepositories();

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;
    // Readable dock tab labels in dark themes (see the .cpp).
    void fixDockStyle();

private:
    void buildUi();
    void buildMenus();
    void buildShortcuts();
    void restoreGeometryState();
    void rebuildViewMenu();
    void rebuildRecentMenu();
    void attachToPanes(const QList<QAction *> &actions);
    void addTab(RepoTab *tab, int index);
    void onCurrentTabChanged(int index);
    void tabMenu(const QPoint &pos);
    void updateWelcome();
    void saveSession() const;
    void addRecent(const QString &path);
    void removeRecent(const QString &path);
    void openFromUser(const QString &path);
    void openRepositoryDialog();
    void doPrefs();
    void changeFontSize(int delta);

    QList<RepoTab *> m_tabs;   // in tab bar order
    bool m_restoring = false;   // suppresses saving the session

    QStackedWidget *m_central;
    WelcomePage *m_welcome;
    QWidget *m_workspace;
    QTabBar *m_tabBar;

    // Bump when the panes change so an incompatible saved layout falls back to the default.
    static constexpr int DockStateVersion = 2;   // 2: Commit pane
    ads::CDockManager *m_dockManager;
    ads::CDockWidget *m_commitsPane, *m_diffPane, *m_filesPane, *m_commitPane;
    // Each pane shows the current tab's widget of a stack holding every tab's.
    QStackedWidget *m_commitsStack, *m_diffStack, *m_filesStack, *m_commitStack;
    QByteArray m_defaultDockState;

    QMenu *m_viewMenu, *m_recentMenu;
    QActionGroup *m_viewGroup = nullptr;
    QList<QAction *> m_tabActions;   // enabled only while a repository is open
};

} // namespace gitq
