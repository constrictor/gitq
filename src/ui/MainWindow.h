// The gitk main window.
#pragma once

#include "Settings.h"
#include "git/DiffEngine.h"
#include "git/LogLoader.h"
#include "git/Ops.h"
#include "git/NearbyRefs.h"
#include "git/Repository.h"
#include "git/Search.h"
#include "ui/DiffView.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QTemporaryDir>
#include <memory>
#include <optional>

class QActionGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QProgressBar;
class QRadioButton;
class QSpinBox;
class QSplitter;
class QTimer;
class QToolButton;

namespace gitq {

class CommitListView;
class CommitModel;
class FileListModel;
class GraphDelegate;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const QString &repoPath, const QStringList &args, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Select a commit if it is in the view; returns false otherwise.
    bool selectCommit(const Oid &id, bool addToHistory = true);
    // Directory relative paths on the command line are relative to (empty: repo root).
    void setCommandLinePrefix(const QString &prefix) { m_prefix = prefix; }

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    struct DiffJob {
        DiffResult diff;
        QStringList tree;
        quint64 seq = 0;
    };

    // construction
    void buildMenus();
    void buildUi();
    void buildShortcuts();
    void restoreGeometryState();
    void rebuildViewMenu();

    // loading
    void startLoad(bool keepSelection);
    void onCommitsLoaded(const CommitPtrList &batch);
    void onLoadFinished(const QString &error);
    void rereadRefs();
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

    // views
    void newView();
    void editView();
    void deleteView();
    void showView(int index);

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
    void blame(const QString &path, const Oid &at, int line);
    void showLineOrigin(const DiffLineInfo &info);
    void showRefsDialog();
    void doPrefs();
    void applySettings();
    void changeFontSize(int delta);
    void reportOp(const OpResult &r, const QString &what);

    // state
    Repository m_repo;
    QString m_repoPath;
    QString m_prefix;
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

    QSplitter *m_vsplit, *m_hsplit;
    QLineEdit *m_shaEdit;
    QToolButton *m_backBtn, *m_fwdBtn;
    QLabel *m_rowLabel;
    QProgressBar *m_progress;
    QToolButton *m_findDownBtn, *m_findUpBtn;
    QComboBox *m_findType, *m_findMode, *m_findField;
    QLineEdit *m_findEdit;
    QTimer *m_findTimer;
    QLineEdit *m_searchEdit;
    QRadioButton *m_modeDiff, *m_modeOld, *m_modeNew;
    QSpinBox *m_contextSpin;
    QCheckBox *m_ignoreSpace;
    QComboBox *m_wordDiff;
    DiffView *m_diff;
    QRadioButton *m_patchMode, *m_treeMode;
    QListView *m_files;
    FileListModel *m_fileModel;
    QMenu *m_viewMenu;
    QActionGroup *m_viewGroup = nullptr;
    QAction *m_editViewAct, *m_delViewAct;

    // The all-zero id is a valid selection (local uncommitted changes), hence the flag.
    Oid m_selected;
    bool m_hasSelection = false;
    std::optional<Oid> m_pendingSelect;
    bool m_selectFirstWhenDone = false;
    QList<Oid> m_history;
    int m_historyIndex = -1;
    bool m_navigating = false;

    bool m_rangeDiff = false;
    bool m_rangeValid = false;
    Oid m_diffFrom, m_diffTo;
    DiffResult m_lastDiff;
    quint64 m_diffSeq = 0;
    QFutureWatcher<DiffJob> m_diffWatcher;
    QFutureWatcher<NearbyRefs> m_nearbyWatcher;
    std::shared_ptr<std::atomic<bool>> m_nearbyCancel;
    FindSpec m_findSpec;
    bool m_contentSearchDone = true;

    QList<std::shared_ptr<QTemporaryDir>> m_tempDirs;
};

} // namespace gitq
