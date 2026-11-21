// One open repository: its commit list, diff and file list, and everything behind them.
// MainWindow shows one RepoTab per tab. The RepoTab widget itself is the content of the Commits
// pane; diffPane() and filesPane() are the contents of the Diff and Files panes.
#pragma once

#include "Settings.h"
#include "git/DiffEngine.h"
#include "git/LogLoader.h"
#include "git/Ops.h"
#include "git/NearbyRefs.h"
#include "git/Repository.h"
#include "git/Search.h"
#include "git/Staging.h"
#include "ui/DiffView.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <QWidget>
#include <memory>
#include <optional>

class QAction;
class QCheckBox;
class QFileSystemWatcher;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QProgressBar;
class QRadioButton;
class QSpinBox;
class QTimer;
class QToolButton;

namespace gitq {

class Choice;
class CommitPane;
class FileListDelegate;
class CommitListView;
class CommitModel;
class FileListModel;
class GraphDelegate;

class RepoTab : public QWidget {
    Q_OBJECT
public:
    // `prefix`: directory that relative paths in `args` are relative to (empty: repo root).
    RepoTab(Repository repo, const QStringList &args, const QString &prefix, QWidget *parent = nullptr);
    ~RepoTab() override;

    QWidget *diffPane() const { return m_diffPane; }
    QWidget *filesPane() const { return m_filesPane; }
    QWidget *commitPane() const;

    QString repoPath() const { return m_repoPath; }   // .git directory: identifies the repository
    QString displayPath() const;                      // work tree (or .git directory if bare)
    QString title() const;                            // short name for the tab
    QStringList args() const { return m_args; }
    QString prefix() const { return m_prefix; }

    // Loads the commits the first time the tab is shown.
    void activate();
    bool isActivated() const { return m_activated; }

    // Select a commit if it is in the view; returns false otherwise.
    bool selectCommit(const Oid &id, bool addToHistory = true);
    // Select a commit once it is loaded (now, if it is), and scroll its diff to `line` of `file`
    // (new side) if given; reports in the status bar if the view doesn't contain it.
    void revealCommit(const Oid &id, const QString &file = {}, int line = 0);

    // Commands of the window's menus.
    void startLoad(bool keepSelection);
    void rereadRefs();
    void showRefsDialog();
    void startGitGui();
    const QList<ViewDef> &views() const { return m_views; }
    int currentView() const { return m_currentView; }
    void showView(int index);
    void newView();
    void editView();
    void deleteView();

    // Re-reads the local changes (index, work tree, HEAD) and updates the local changes rows, their
    // diff and the Commit pane if they changed. Called when the git directory changes, when the
    // window is activated and after staging operations.
    void refreshLocalChanges();
    // Restores the files of the last discard / delete.
    bool canUndoDiscard() const { return !m_discards.isEmpty(); }
    void undoDiscard();

    // Settings changed (preferences, font size, theme): re-apply them and redisplay.
    void applySettings();
    void settingsChanged(bool reload);
    QByteArray listHeaderState() const;

signals:
    void statusMessage(const QString &text, int timeout);
    void viewsChanged();
    void raiseRequested();   // a blame window navigated to a commit of this tab
    // Open the repository at `path` (a submodule's) in a tab and reveal `commit` there (if not null).
    void openRepositoryRequested(const QString &path, const gitq::Oid &commit, const QString &file, int line);

private:
    struct DiffJob {
        DiffResult diff;
        QStringList tree;
        quint64 seq = 0;
    };
    struct LocalState {
        bool index = false, workdir = false;   // local changes rows that should exist
        Oid head;
        StagedSummary staged;
    };

    // construction
    void buildUi();
    void buildShortcuts();

    // loading
    void onCommitsLoaded(const CommitPtrList &batch);
    void onLoadFinished(const QString &error);
    void refreshAfterChange();
    LogOptions currentOptions(QString *error);

    // selection & display
    void onCurrentRowChanged(int row);
    void showCommit(const Oid &id);
    void showRangeDiff(const Oid &from, const Oid &to);
    void startDiff();
    void onDiffReady();
    void fillFileList();
    DiffOptions diffOptions() const;
    QString headlineOf(const Oid &id);
    int selectedRow() const;
    void selectRow(int row);
    void moveSelection(int delta);
    void goHistory(int delta);
    void goToParent(int n);
    void gotoCommitFromField();
    void onFileClicked(const QModelIndex &idx);

    // find
    void findChanged();
    void runFind();
    void findNext(int dir);
    FindSpec currentFindSpec() const;

    // context menus / commands
    void rowMenu(int row, const QPoint &pos);
    void refMenu(int row, const RefInfo &ref, const QPoint &pos);
    void fileMenu(const QPoint &pos);
    void diffMenu(const DiffLineInfo &info, const QPoint &pos);
    void makePatch(const Oid &id);
    void writeCommit(const Oid &id);
    void createTag(const Oid &id);
    void createBranch(const Oid &id);
    void cherryPick(const Oid &id);
    void revertCommit(const Oid &id);
    void resetHere(const Oid &id);
    void findCommonDescendant(const Oid &id);
    void compareWithMarked(const Oid &id);
    void copyReference(const Oid &id);
    void checkoutBranch(const RefInfo &ref);
    void renameBranch(const RefInfo &ref);
    void removeBranch(const RefInfo &ref);
    void externalDiff(const QString &path);
    // Commit of the diff's old side (first parent) or new side, or null if there is none.
    std::optional<Oid> sideCommit(bool oldSide) const;
    // Blame `path` on the old or new side of the diff; inside a submodule, in the submodule.
    void blame(const QString &path, bool oldSide, int line);
    // Open the submodule at or containing `path` in a tab, at `commit` (null: the one shown).
    void openSubmodule(const QString &path, const Oid &commit);
    bool canOpenSubmodule(const QString &path) const;
    void showLineOrigin(const DiffLineInfo &info);
    void reportOp(const OpResult &r, const QString &what);

    // commit preparation
    // Which local changes row (if any) the diff shows, i.e. which buttons it gets.
    DiffView::ActionSet localActionSet() const;
    void updateLocalUi();
    void updateCommitPane();
    void onLocalStateReady();
    // The files selected in the file list (folder rows: their files), or just `clicked` if it is
    // not among them.
    QStringList selectedFiles(const QString &clicked) const;
    // The files listed under the folder row `folder` (grouped file list).
    QStringList folderFiles(QString folder) const;
    // `paths` plus the old paths of renamed files among them.
    QStringList withOldPaths(const QStringList &paths) const;
    QStringList allLocalFiles() const;
    void fileAction(const QStringList &paths, int action);
    void hunkAction(const QString &path, int hunk, int action);
    void afterLocalOp(const OpResult &r, const QString &what, const QList<DiscardBackup> &backups);
    void onCommitted(const OpResult &r);

    // state
    Repository m_repo;
    QString m_repoPath;
    QStringList m_args;
    QString m_prefix;
    bool m_activated = false;
    RefSet m_refs;
    QList<ViewDef> m_views;
    int m_currentView = 0;
    LogOptions m_opts;
    QElapsedTimer m_loadTimer;

    CommitModel *m_model;
    GraphDelegate *m_delegate;
    CommitListView *m_list;
    LogLoader *m_loader;
    SearchWorker *m_searcher;

    // Pane contents other than this widget; owned by this tab but parented to the window's panes.
    QPointer<QWidget> m_diffPane, m_filesPane;
    QLineEdit *m_shaEdit;
    QToolButton *m_backBtn, *m_fwdBtn;
    QLabel *m_rowLabel;
    QProgressBar *m_progress;
    QLineEdit *m_findEdit;
    QAction *m_findUp, *m_findDown, *m_findCase, *m_findRegex;
    Choice *m_findWhat;   // all fields, headline, message, author, committer, paths, -S, -G
    QTimer *m_findTimer;
    QLineEdit *m_searchEdit;
    QToolButton *m_modeDiff, *m_modeOld, *m_modeNew;
    QToolButton *m_unified, *m_split;
    QSpinBox *m_contextSpin;
    QToolButton *m_ignoreSpace;
    Choice *m_wordDiff;
    Choice *m_submoduleMode;
    DiffView *m_diff;
    QToolButton *m_patchMode, *m_treeMode;
    QListView *m_files;
    FileListModel *m_fileModel;
    FileListDelegate *m_fileDelegate;
    QToolButton *m_stageAllBtn, *m_unstageAllBtn;
    QPointer<CommitPane> m_commitPane;

    // The all-zero id is a valid selection (local uncommitted changes), hence the flag.
    Oid m_selected;
    bool m_hasSelection = false;
    std::optional<Oid> m_pendingSelect;
    std::optional<Oid> m_revealId;   // revealCommit() before / while loading
    struct PendingLine {
        Oid commit;
        QString path;
        int line = 0;
    };
    std::optional<PendingLine> m_pendingLine;   // scroll there once that commit's diff is shown
    QSet<QString> m_expandedSubmodules;         // submodules whose files are shown (SubmoduleLog)
    bool m_selectFirstWhenDone = false;
    QList<Oid> m_history;
    int m_historyIndex = -1;
    bool m_navigating = false;

    bool m_rangeDiff = false;
    bool m_rangeValid = false;
    QString m_rangeHeader;
    Oid m_diffFrom, m_diffTo;
    DiffResult m_lastDiff;
    quint64 m_diffSeq = 0;
    QFutureWatcher<DiffJob> m_diffWatcher;
    QFutureWatcher<NearbyRefs> m_nearbyWatcher;
    QFutureWatcher<LocalState> m_localWatcher;
    bool m_localPending = false;    // refresh again when the running one is done
    quint64 m_quietDiffSeq = 0;     // a refresh of the diff shown: redisplay only if it changed
    QFileSystemWatcher *m_gitWatcher = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QList<QList<DiscardBackup>> m_discards;   // backups of discards / deletes, oldest first
    std::shared_ptr<std::atomic<bool>> m_nearbyCancel;
    FindSpec m_findSpec;
    bool m_contentSearchDone = true;

    QList<std::shared_ptr<QTemporaryDir>> m_tempDirs;
};

} // namespace gitq
