// GUI smoke tests: drive the main window like a user would.
#include "Settings.h"
#include "TestRepo.h"
#include "model/CommitModel.h"
#include "model/FileListModel.h"
#include "ui/Choice.h"
#include "ui/CommitListView.h"
#include "ui/DiffView.h"
#include "ui/MainWindow.h"
#include "ui/PrefsDialog.h"
#include "ui/RepoTab.h"
#include "ui/ViewDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QTabBar>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTreeView>

#include <DockManager.h>
#include <DockWidget.h>

using namespace gitq;

class TestMainWindow : public QObject {
    Q_OBJECT
    LibGit2Init init;
    std::unique_ptr<test::TestRepo> repo;
    QString mergeId, fixId;

    template <typename T> T *child(QWidget *w, const char *name) {
        T *c = w->findChild<T *>(QLatin1String(name));
        if (!c) qFatal("no child %s", name);
        return c;
    }

    void shot(QWidget *w, const QString &name) {
        QString dir = qEnvironmentVariable("GITQ_TEST_SHOTS");
        if (!dir.isEmpty()) w->grab().save(QDir(dir).filePath(name + ".png"));
    }

    CommitModel *model(QWidget *w) { return static_cast<CommitModel *>(child<QTreeView>(w, "commitList")->model()); }

    void waitLoaded(QWidget *w) {
        QTRY_VERIFY_WITH_TIMEOUT(model(w)->rowCount() > 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!child<QLineEdit>(w, "shaEdit")->text().isEmpty(), 10000);
        QTest::qWait(200);
    }

    QString diffText(QWidget *w) { return child<DiffView>(w, "diffView")->toPlainText(); }

    void selectId(QWidget *w, const QString &id) {
        auto *sha = child<QLineEdit>(w, "shaEdit");
        sha->setText(id);
        QMetaObject::invokeMethod(sha, "returnPressed");
        QTRY_VERIFY(diffText(w).contains(QStringLiteral("----------")) || diffText(w).contains(QStringLiteral("Author:")));
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("gitq-test"));
        Settings::instance().load();
        QSettings().remove(QStringLiteral("window/dockState"));   // tests expect the default layout
        QSettings().remove(QStringLiteral("session"));            // open tabs, recent repositories
        Settings::instance().showNearTags = true;
        Settings::instance().commitListMultiline = false;
        repo = std::make_unique<test::TestRepo>();
        auto &r = *repo;
        r.commit("Initial commit", {{"README", "hello\n"}, {"src/main.c", "int main;\nint x = 1;\n"}});
        r.git({"tag", "v0.1"});
        r.git({"checkout", "-q", "-b", "feature"});
        r.commit("Add feature", {{"src/feature.c", "feature\n"}, {"src/main.c", "int main;\nint x = 2;\n"}});
        r.git({"checkout", "-q", "master"});
        fixId = r.commit("Fix main", {{"src/main.c", "int main;\nint x = 3;\n"}});
        r.git({"merge", "-q", "feature"}, false);
        r.write("src/main.c", "int main;\nint x = 4;\n");
        r.git({"add", "-A"});
        r.m_tick++;
        r.git({"commit", "-q", "-m", "Merge feature"});
        mergeId = r.rev("HEAD");
        r.commit("After merge", {{"README", "hello world\n"}});
    }

    void loadsAndSelectsHead() {
        MainWindow w(repo->path(), {QStringLiteral("--all")});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        QCOMPARE(model(&w)->rowCount(), 5);
        QCOMPARE(child<QLineEdit>(&w, "shaEdit")->text(), repo->rev("HEAD"));
        QVERIFY(diffText(&w).contains(QStringLiteral("Author: Tester <tester@example.com>")));
        QVERIFY(diffText(&w).contains(QStringLiteral("---------- README ----------")));
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral("Branches: master")));
        QVERIFY(child<QLabel>(&w, "rowLabel")->text().contains(QStringLiteral("1 / 5")));
        shot(&w, QStringLiteral("01-head"));
    }

    void mergeShowsCombinedDiff() {
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        selectId(&w, mergeId.left(8));
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral("@@@")));
        QVERIFY(diffText(&w).contains(QStringLiteral("++int x = 4;")));
        QVERIFY(diffText(&w).contains(QStringLiteral("Parent: ")));
        shot(&w, QStringLiteral("02-merge"));
    }

    void findAndNavigate() {
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        auto *find = child<QLineEdit>(&w, "findEdit");
        find->setText(QStringLiteral("feature"));
        QTRY_COMPARE(model(&w)->highlighted().size(), 2);   // "Add feature", "Merge feature"
        QMetaObject::invokeMethod(find, "returnPressed");
        QTRY_COMPARE(child<QLineEdit>(&w, "shaEdit")->text(), mergeId);

        child<Choice>(&w, "findWhat")->setCurrentIndex(5);   // touching paths
        find->setText(QStringLiteral("src/feature.c"));
        QTRY_COMPARE(model(&w)->highlighted().size(), 1);
        shot(&w, QStringLiteral("03-find"));
    }

    void treeModeAndWordDiff() {
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        selectId(&w, fixId);
        child<Choice>(&w, "wordDiff")->setCurrentIndex(1);   // markup words
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral("[-1-]{+3+}")));
        child<Choice>(&w, "wordDiff")->setCurrentIndex(2);   // color words
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral(" int x = 13;")));
        shot(&w, QStringLiteral("05-colorwords"));
        child<Choice>(&w, "wordDiff")->setCurrentIndex(0);
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral("+int x = 3;")));

        child<QRadioButton>(&w, "treeMode")->setChecked(true);
        auto *files = child<QListView>(&w, "fileList");
        QTRY_COMPARE(files->model()->rowCount(), 2);   // README, src/main.c at that commit
        QModelIndex idx = files->model()->index(1, 0);
        QCOMPARE(idx.data(FileListModel::PathRole).toString(), QStringLiteral("src/main.c"));
        emit files->clicked(idx);
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral("int x = 3;")));
        shot(&w, QStringLiteral("04-tree"));
    }

    void submodules() {
        test::TestRepo sub, r;
        const QString s1 = sub.commit("s1", {{"x.c", "1\n2\n3\n"}});
        r.commit("init", {{"main.c", "m\n"}});
        r.addSubmodule(sub.path(), "lib/sub");
        r.commit("Add sub");
        const QString s2 = sub.commit("Change x in sub", {{"x.c", "1\ntwo\n3\n"}});
        r.moveSubmodule("lib/sub", s2);
        r.commit("Bump sub");

        MainWindow w(r.path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        auto *diff = child<DiffView>(&w, "diffView");
        auto *files = child<QListView>(&w, "fileList");
        auto fileRows = [files] {
            QStringList out;
            for (int i = 0; i < files->model()->rowCount(); ++i)
                out << files->model()->index(i, 0).data(FileListModel::PathRole).toString();
            return out;
        };

        // Commits: the submodule's commits in between; its files on request.
        auto *mode = child<Choice>(&w, "submoduleMode");
        mode->setCurrentIndex(DiffOptions::SubmoduleLog);
        QTRY_VERIFY(diffText(&w).contains("Submodule lib/sub " + s1.left(7) + ".." + s2.left(7)));
        QVERIFY(diffText(&w).contains("  > " + s2.left(7) + " Change x in sub"));
        QVERIFY(diffText(&w).contains("[show files]"));
        QCOMPARE(fileRows(), (QStringList{QString(), "lib/sub"}));
        emit diff->submoduleToggled("lib/sub");
        QTRY_VERIFY(diffText(&w).contains("+two"));
        QVERIFY(diffText(&w).contains("---------- lib/sub/x.c ----------"));
        QVERIFY(diffText(&w).contains("[hide files]"));
        QCOMPARE(fileRows(), (QStringList{QString(), "lib/sub", "lib/sub/x.c"}));
        shot(&w, QStringLiteral("09-submodule"));
        emit diff->submoduleToggled("lib/sub");
        QTRY_VERIFY(!diffText(&w).contains("+two"));

        // Full diff: every submodule's files; Summary: git's lines.
        mode->setCurrentIndex(DiffOptions::SubmoduleDiff);
        QTRY_VERIFY(diffText(&w).contains("+two"));
        QVERIFY(!diffText(&w).contains("[hide files]"));
        mode->setCurrentIndex(DiffOptions::SubmoduleSummary);
        QTRY_VERIFY(diffText(&w).contains("+Subproject commit " + s2));
        QCOMPARE(fileRows(), (QStringList{QString(), "lib/sub"}));
        mode->setCurrentIndex(DiffOptions::SubmoduleLog);

        // The submodule opens in a tab of its own, at the commit asked for.
        QTRY_VERIFY(diffText(&w).contains("[open in tab]"));
        emit diff->submoduleOpenRequested("lib/sub", Oid::fromHex(s1));
        QTRY_COMPARE(w.tabCount(), 2);
        RepoTab *t = w.currentTab();
        QCOMPARE(QFileInfo(t->displayPath()).canonicalFilePath(), QFileInfo(r.path() + "/lib/sub").canonicalFilePath());
        QTRY_COMPARE(t->findChild<QLineEdit *>("shaEdit")->text(), s1);
        // Asking again switches to that tab.
        w.setCurrentTab(0);
        emit diff->submoduleOpenRequested("lib/sub", Oid());
        QCOMPARE(w.tabCount(), 2);
        QCOMPARE(w.currentTab(), t);
        QTRY_COMPARE(t->findChild<QLineEdit *>("shaEdit")->text(), s2);
        // Tree mode lists the submodule's files.
        w.setCurrentTab(0);
        for (QRadioButton *b : w.findChildren<QRadioButton *>("treeMode"))
            if (b->isVisible()) b->setChecked(true);
        QTRY_COMPARE(fileRows(), (QStringList{".gitmodules", "lib/sub/x.c", "main.c"}));
        emit files->clicked(files->model()->index(1, 0));
        QTRY_VERIFY(diffText(&w).contains("two"));

        Settings::instance().submoduleDiff = DiffOptions::SubmoduleLog;
    }

    void keyBindings() {
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        waitLoaded(&w);
        auto *list = child<QTreeView>(&w, "commitList");
        auto *sha = child<QLineEdit>(&w, "shaEdit");
        auto *diff = child<DiffView>(&w, "diffView");
        auto row = [&] { return list->currentIndex().row(); };
        list->setFocus();
        QTest::keyClick(list, Qt::Key_N);
        QTRY_COMPARE(row(), 1);
        QTest::keyClick(list, Qt::Key_J);
        QTRY_COMPARE(row(), 2);
        QTest::keyClick(list, Qt::Key_P);
        QTRY_COMPARE(row(), 1);
        diff->setFocus();
        QTest::keyClick(diff, Qt::Key_N);
        QTRY_COMPARE(row(), 2);
        QTest::keyClick(diff, Qt::Key_Left);   // back in history
        QTRY_COMPARE(row(), 1);
        QTest::keyClick(diff, Qt::Key_Right);
        QTRY_COMPARE(row(), 2);
        QTest::keyClick(diff, Qt::Key_G);
        QTRY_VERIFY(sha->hasFocus());
        QTest::keyClicks(sha, "n");   // typing goes to the field, not the shortcut
        QCOMPARE(row(), 2);
        QCOMPARE(sha->text(), QStringLiteral("n"));
        sha->setText(repo->rev("HEAD").left(7));
        QTest::keyClick(sha, Qt::Key_Return);
        QTRY_COMPARE(row(), 0);
        QTest::keyClick(list, Qt::Key_1, Qt::ControlModifier);   // first parent
        QTRY_COMPARE(row(), 1);
    }

    void localChangesRows() {
        repo->write("README", "edited in work tree\n");
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        QCOMPARE(model(&w)->rowCount(), 6);
        QVERIFY(model(&w)->commitAt(0)->id.isWorkdir());
        QCOMPARE(child<QLineEdit>(&w, "shaEdit")->text(), repo->rev("HEAD"));   // HEAD stays selected
        auto *list = child<QTreeView>(&w, "commitList");
        list->setCurrentIndex(model(&w)->index(0, 0));
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral("---------- README ----------")));
        QVERIFY(diffText(&w).contains(QStringLiteral("+edited in work tree")));
        QVERIFY(diffText(&w).startsWith(QStringLiteral("Local uncommitted changes")));
        repo->git({"checkout", "--", "README"});
    }

    void multilineCommitList() {
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        auto *list = child<QTreeView>(&w, "commitList");
        QAction *toggle = nullptr;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == QStringLiteral("&Multi-line commit list")) toggle = a;
        QVERIFY(toggle);
        QVERIFY(!toggle->isChecked());
        const int singleHeight = list->sizeHintForRow(0);
        QVERIFY(list->header()->isVisible());
        shot(&w, QStringLiteral("05-table-list"));

        toggle->trigger();
        QVERIFY(Settings::instance().commitListMultiline);
        QVERIFY(list->sizeHintForRow(0) > singleHeight * 2 - 1);
        QVERIFY(list->header()->isSectionHidden(CommitModel::AuthorColumn));
        QVERIFY(list->header()->isSectionHidden(CommitModel::DateColumn));
        QVERIFY(!list->header()->isVisible());
        QCOMPARE(list->visualRect(model(&w)->index(1, 0)).top(), list->sizeHintForRow(0));
        shot(&w, QStringLiteral("06-multiline-list"));

        toggle->trigger();
        QVERIFY(!Settings::instance().commitListMultiline);
        QCOMPARE(list->sizeHintForRow(0), singleHeight);
        QVERIFY(!list->header()->isSectionHidden(CommitModel::AuthorColumn));
        QVERIFY(!list->header()->isSectionHidden(CommitModel::DateColumn));
        QVERIFY(list->header()->isVisible());
        QVERIFY(list->header()->sectionSize(CommitModel::AuthorColumn) > 0);
    }

    void hoverRow() {
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        waitLoaded(&w);
        auto *list = child<CommitListView>(&w, "commitList");
        QCOMPARE(list->hoverRow(), -1);
        const QRect r = list->visualRect(model(&w)->index(2, 0));
        QTest::mouseMove(list->viewport(), r.center());
        QTRY_COMPARE(list->hoverRow(), 2);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(list, &leave);
        QCOMPARE(list->hoverRow(), -1);
    }

    void viewDialogRoundTrip() {
        ViewDef v;
        v.name = QStringLiteral("My view");
        v.remember = true;
        v.args = QStringList{"--all", "--author=bob", "--since=2 weeks ago", "--max-count=50", "--first-parent",
                             "--topo-order", "master", "^v1", "--", "src", "doc"};
        ViewDialog dlg(v, false);
        dlg.show();
        shot(&dlg, QStringLiteral("06-viewdialog"));
        ViewDef out = dlg.view();
        QCOMPARE(out.name, v.name);
        QVERIFY(out.remember);
        QCOMPARE(QSet<QString>(out.args.cbegin(), out.args.cend()), QSet<QString>(v.args.cbegin(), v.args.cend()));
        QCOMPARE(out.args.mid(out.args.indexOf("--")), (QStringList{"--", "src", "doc"}));
        PrefsDialog prefs;
        prefs.show();
        shot(&prefs, QStringLiteral("07-prefs"));
    }

    void pathLimitedView() {
        QString old = QDir::currentPath();
        QDir::setCurrent(repo->path());
        MainWindow w(repo->path(), {QStringLiteral("--"), QStringLiteral("README")});
        QDir::setCurrent(old);
        w.resize(1200, 900);
        w.show();
        QTRY_VERIFY_WITH_TIMEOUT(model(&w)->rowCount() > 0, 10000);
        QTest::qWait(200);
        QCOMPARE(model(&w)->rowCount(), 2);
    }

    void dockablePanes() {
        auto reset = [](QWidget *w) {
            for (QAction *a : w->findChildren<QAction *>())
                if (a->text() == QStringLiteral("&Reset layout")) return a->trigger();
            qFatal("no Reset layout action");
        };
        {
            MainWindow w(repo->path(), {});
            w.resize(1200, 900);
            w.show();
            QVERIFY(QTest::qWaitForWindowActive(&w));
            waitLoaded(&w);
            auto *commits = child<ads::CDockWidget>(&w, "commitsPane");
            auto *diffPane = child<ads::CDockWidget>(&w, "diffPane");
            auto *files = child<ads::CDockWidget>(&w, "filesPane");
            QVERIFY(commits->dockAreaWidget() != files->dockAreaWidget());
            QVERIFY(diffPane->dockAreaWidget() != files->dockAreaWidget());

            // Closing a pane hides it; the View menu brings it back.
            diffPane->toggleViewAction()->trigger();
            QVERIFY(!child<DiffView>(&w, "diffView")->isVisible());
            reset(&w);
            QTRY_VERIFY(child<DiffView>(&w, "diffView")->isVisible());

            // Window shortcuts still work with focus in a floating pane.
            diffPane->setFloating();
            QVERIFY(diffPane->isFloating());
            QWidget *floating = diffPane->window();
            QVERIFY(floating != &w);
            floating->activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(floating));
            auto *diff = child<DiffView>(floating, "diffView");
            diff->setFocus();
            QString before = child<QLineEdit>(&w, "shaEdit")->text();
            QTest::keyClick(diff, Qt::Key_N);
            QTRY_VERIFY(child<QLineEdit>(&w, "shaEdit")->text() != before);

            // Tab the file list with the commit list; the layout is saved on close.
            reset(&w);
            w.findChild<ads::CDockManager *>()->addDockWidgetTabToArea(files, commits->dockAreaWidget());
            QCOMPARE(files->dockAreaWidget(), commits->dockAreaWidget());
            w.close();
        }
        {
            // A pane hidden behind another tab is detached from the widget tree, so look it up globally.
            auto pane = [](const char *name) {
                for (QWidget *x : QApplication::allWidgets())
                    if (x->objectName() == QLatin1String(name)) return static_cast<ads::CDockWidget *>(x);
                qFatal("no pane %s", name);
            };
            MainWindow w(repo->path(), {});
            w.resize(1200, 900);
            w.show();
            auto *commits = pane("commitsPane");
            auto *files = pane("filesPane");
            QCOMPARE(files->dockAreaWidget(), commits->dockAreaWidget());
            reset(&w);
            QVERIFY(files->dockAreaWidget() != commits->dockAreaWidget());
            w.close();
        }
    }

    void tabsAndSession() {
        QSettings().remove(QStringLiteral("session"));
        test::TestRepo other;
        other.commit("Other repository", {{"a.txt", "a\n"}});
        auto canon = [](const QString &p) { return QFileInfo(p).canonicalFilePath(); };
        auto tabModel = [](RepoTab *t) {
            return static_cast<CommitModel *>(t->findChild<QTreeView *>(QStringLiteral("commitList"))->model());
        };
        {
            MainWindow w(repo->path(), {});
            w.resize(1200, 900);
            w.show();
            waitLoaded(&w);
            RepoTab *first = w.currentTab();
            w.openRepository(other.path());
            QCOMPARE(w.tabCount(), 2);
            QCOMPARE(canon(w.currentTab()->displayPath()), canon(other.path()));
            QTRY_COMPARE(tabModel(w.currentTab())->rowCount(), 1);
            QCOMPARE(child<QTabBar>(&w, "repoTabs")->tabText(1), QFileInfo(other.path()).fileName());

            // Opening an open repository switches to its tab; new arguments reopen it in place.
            w.openRepository(repo->path());
            QCOMPARE(w.tabCount(), 2);
            QCOMPARE(w.currentTab(), first);
            w.openRepository(other.path(), {QStringLiteral("--"), QStringLiteral("a.txt")});
            QCOMPARE(w.tabCount(), 2);
            QCOMPARE(w.tab(0), first);
            QCOMPARE(w.currentTab(), w.tab(1));
            QCOMPARE(w.tab(1)->args(), (QStringList{"--", "a.txt"}));
            QCOMPARE(w.tab(1)->views().size(), 2);   // "All files" + "Command line"
            w.setCurrentTab(0);
            w.close();   // saves the session
        }
        {
            // The previous tabs come back; only the current one loads.
            MainWindow w;
            w.restoreSession();
            w.resize(1200, 900);
            w.show();
            QCOMPARE(w.tabCount(), 2);
            QCOMPARE(w.currentTab(), w.tab(0));
            QCOMPARE(canon(w.tab(0)->displayPath()), canon(repo->path()));
            QCOMPARE(w.tab(1)->args(), (QStringList{"--", "a.txt"}));
            QTRY_COMPARE(tabModel(w.tab(0))->rowCount(), 5);
            QVERIFY(!w.tab(1)->isActivated());
            w.setCurrentTab(1);
            QTRY_COMPARE(tabModel(w.tab(1))->rowCount(), 1);

            // Without tabs the welcome page lists the recent repositories, most recent first.
            w.closeTab(0);
            w.closeTab(0);
            QCOMPARE(w.tabCount(), 0);
            QCOMPARE(w.windowTitle(), QStringLiteral("gitq"));
            auto *recent = child<QListWidget>(&w, "recentList");
            QTRY_VERIFY(recent->isVisible());
            QCOMPARE(recent->count(), 2);
            QCOMPARE(canon(recent->item(0)->data(Qt::UserRole).toString()), canon(other.path()));
            QCOMPARE(canon(recent->item(1)->data(Qt::UserRole).toString()), canon(repo->path()));
            shot(&w, QStringLiteral("08-welcome"));
            emit recent->itemClicked(recent->item(1));
            QTRY_COMPARE(w.tabCount(), 1);
            QCOMPARE(canon(w.currentTab()->displayPath()), canon(repo->path()));
            QTRY_COMPARE(tabModel(w.currentTab())->rowCount(), 5);
            w.close();
        }
        {
            // All tabs were closed but one: the next run restores just that.
            MainWindow w;
            w.restoreSession();
            QCOMPARE(w.tabCount(), 1);
            w.close();
        }
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
