// GUI smoke tests: drive the main window like a user would.
#include "Settings.h"
#include "TestRepo.h"
#include "model/CommitModel.h"
#include "model/FileListModel.h"
#include "ui/Choice.h"
#include "ui/CommitListView.h"
#include "ui/CommitPane.h"
#include "ui/DiffView.h"
#include "ui/MainWindow.h"
#include "ui/PrefsDialog.h"
#include "ui/RepoTab.h"
#include "ui/ViewDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QClipboard>
#include <QScrollBar>
#include <QTextBlock>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QTabBar>
#include <QToolButton>
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
        QTRY_VERIFY_WITH_TIMEOUT(!selectedId(w).isEmpty(), 10000);
        QTest::qWait(200);
    }

    // The id of the commit selected in the list of the tab on display (or of the given tab).
    QString selectedId(QWidget *w) {
        QTreeView *list = nullptr;
        for (QTreeView *v : w->findChildren<QTreeView *>("commitList"))
            if (v->isVisible() || !list) list = v;
        if (!list) return {};
        const QModelIndex i = list->currentIndex();
        const CommitInfo *c = i.isValid() ? static_cast<CommitModel *>(list->model())->commitAt(i.row()) : nullptr;
        return c ? c->id.hex() : QString();
    }

    bool m_selectedIsWorkdir(QWidget *w) {
        const QModelIndex i = child<QTreeView>(w, "commitList")->currentIndex();
        return i.isValid() && model(w)->commitAt(i.row())->id.isWorkdir();
    }

    QString diffText(QWidget *w) { return child<DiffView>(w, "diffView")->toPlainText(); }
    QStringList diffFiles(QWidget *w) { return child<DiffView>(w, "diffView")->files(); }

    void selectId(QWidget *w, const QString &id) {
        // Through the find bar's "go to commit" mode, leaving the find bar as it was.
        auto *find = child<QLineEdit>(w, "findEdit");
        auto *what = child<Choice>(w, "findWhat");
        const int before = what->currentIndex();
        what->setCurrentIndex(8);
        find->setText(id);
        QMetaObject::invokeMethod(find, "returnPressed");
        find->clear();
        what->setCurrentIndex(before);
        QTRY_VERIFY(!diffFiles(w).isEmpty() || diffText(w).contains(QStringLiteral("Parent: ")));
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
        QCOMPARE(selectedId(&w), repo->rev("HEAD"));
        QVERIFY(diffText(&w).contains(QStringLiteral("Tester <tester@example.com>")));
        QVERIFY(diffText(&w).contains(QStringLiteral("After merge")));   // the subject heading
        QVERIFY(diffFiles(&w).contains(QStringLiteral("README")));
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral("Branches: master")));
        // The find bar sits above the list; no SHA1 row below it.
        QVERIFY(child<QLineEdit>(&w, "findEdit")->mapTo(&w, QPoint()).y() <
                child<QTreeView>(&w, "commitList")->mapTo(&w, QPoint()).y());
        QVERIFY(!w.findChild<QLineEdit *>("shaEdit"));
        shot(&w, QStringLiteral("01-head"));

        // Clicking the id chip in the header copies the commit id.
        auto *diff = child<DiffView>(&w, "diffView");
        QTextCursor chip;
        for (QTextBlock b = diff->document()->begin(); b.isValid() && chip.isNull(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().anchorHref().startsWith(QStringLiteral("copy:"))) {
                    chip = QTextCursor(diff->document());
                    chip.setPosition(it.fragment().position());
                    break;
                }
        QVERIFY(!chip.isNull());
        QApplication::clipboard()->clear();
        QTest::mouseClick(diff->viewport(), Qt::LeftButton, {}, diff->cursorRect(chip).center() + QPoint(6, 0));
        QCOMPARE(QApplication::clipboard()->text(), repo->rev("HEAD"));
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
        // Combined diffs number the merge result's lines (for the gutter, blame and line origin).
        QVERIFY(child<DiffView>(&w, "diffView")->scrollToLine(QStringLiteral("src/main.c"), 2, true));
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
        QTRY_COMPARE(selectedId(&w), mergeId);

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

        // Patch mode: line counts per file; grouped by folder, a row for each folder.
        auto *files = child<QListView>(&w, "fileList");
        QTRY_COMPARE(files->model()->rowCount(), 2);   // Comments, src/main.c
        QCOMPARE(files->model()->index(1, 0).data(FileListModel::AddedRole).toInt(), 1);
        QCOMPARE(files->model()->index(1, 0).data(FileListModel::RemovedRole).toInt(), 1);
        child<QToolButton>(&w, "groupFiles")->click();
        QCOMPARE(files->model()->rowCount(), 3);
        QVERIFY(files->model()->index(1, 0).data(FileListModel::IsFolderRole).toBool());
        QCOMPARE(files->model()->index(1, 0).data(FileListModel::NameRole).toString(), QStringLiteral("src/"));
        QCOMPARE(files->model()->index(2, 0).data(FileListModel::NameRole).toString(), QStringLiteral("main.c"));
        shot(&w, QStringLiteral("12-grouped-files"));
        child<QToolButton>(&w, "groupFiles")->click();
        QCOMPARE(files->model()->rowCount(), 2);

        child<QToolButton>(&w, "treeMode")->setChecked(true);
        QTRY_COMPARE(files->model()->index(0, 0).data(FileListModel::PathRole).toString(), QStringLiteral("README"));
        QCOMPARE(files->model()->rowCount(), 2);   // README, src/main.c at that commit
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
        QVERIFY(diffFiles(&w).contains("lib/sub/x.c"));
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
        QTRY_COMPARE(selectedId(t), s1);
        // Asking again switches to that tab.
        w.setCurrentTab(0);
        emit diff->submoduleOpenRequested("lib/sub", Oid());
        QCOMPARE(w.tabCount(), 2);
        QCOMPARE(w.currentTab(), t);
        QTRY_COMPARE(selectedId(t), s2);
        // Tree mode lists the submodule's files.
        w.setCurrentTab(0);
        for (QToolButton *b : w.findChildren<QToolButton *>("treeMode"))
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
        auto *find = child<QLineEdit>(&w, "findEdit");
        auto *what = child<Choice>(&w, "findWhat");
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
        QTRY_VERIFY(find->hasFocus());   // the find bar, in "go to commit" mode
        QCOMPARE(what->currentIndex(), 8);
        QTest::keyClicks(find, "n");   // typing goes to the field, not the shortcut
        QCOMPARE(row(), 2);
        QCOMPARE(find->text(), QStringLiteral("n"));
        QVERIFY(model(&w)->highlighted().isEmpty());   // nothing is searched in this mode
        find->setText(repo->rev("HEAD").left(7));
        QTest::keyClick(find, Qt::Key_Return);
        QTRY_COMPARE(row(), 0);
        find->setText(QStringLiteral("HEAD~2"));   // any revision
        QTest::keyClick(find, Qt::Key_Return);
        QTRY_COMPARE(selectedId(&w), repo->rev("HEAD~2"));
        list->setFocus();
        QTest::keyClick(list, Qt::Key_Home);
        QTRY_COMPARE(row(), 0);
        find->clear();
        what->setCurrentIndex(0);
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
        QCOMPARE(selectedId(&w), repo->rev("HEAD"));   // HEAD stays selected
        auto *list = child<QTreeView>(&w, "commitList");
        list->setCurrentIndex(model(&w)->index(0, 0));
        QTRY_VERIFY(diffFiles(&w).contains(QStringLiteral("README")));
        QVERIFY(diffText(&w).contains(QStringLiteral("+edited in work tree")));
        QVERIFY(diffText(&w).trimmed().startsWith(QStringLiteral("Local uncommitted changes")));
        repo->git({"checkout", "--", "README"});
    }

    void commitPreparation() {
        test::TestRepo r;
        QByteArray text;
        for (int i = 1; i <= 60; ++i) text += QByteArray::number(i) + '\n';
        r.commit("Base", {{"f.txt", text}});
        QByteArray changed = text;
        changed.replace("\n5\n", "\nfive\n").replace("\n30\n", "\nthirty\n");
        r.write("f.txt", changed);
        r.write("new.txt", "brand new\n");

        MainWindow w(r.path(), {});
        w.resize(1200, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        waitLoaded(&w);
        QCOMPARE(model(&w)->rowCount(), 2);   // local changes (untracked files count) + Base
        QVERIFY(model(&w)->commitAt(0)->id.isWorkdir());
        auto *list = child<QTreeView>(&w, "commitList");
        list->setCurrentIndex(model(&w)->index(0, 0));
        QTRY_VERIFY(diffFiles(&w).contains(QStringLiteral("new.txt")));
        QVERIFY(diffFiles(&w).contains(QStringLiteral("f.txt")));
        auto *diff = child<DiffView>(&w, "diffView");
        QCOMPARE(diff->actionSet(), DiffView::WorkdirActions);
        QVERIFY(diff->hunkActions(QStringLiteral("f.txt")) & DiffView::StageAction);
        QCOMPARE(diff->fileActions(QStringLiteral("new.txt")), DiffView::StageAction | DiffView::DeleteAction);
        QCOMPARE(diff->hunkActions(QStringLiteral("new.txt")), 0);   // untracked: whole file only
        shot(&w, QStringLiteral("20-local-changes"));

        // Click the "Stage hunk" button at the right end of f.txt's first hunk line.
        diff->scrollToFile(QStringLiteral("f.txt"));
        QTextBlock hunkLine;
        for (QTextBlock b = diff->document()->begin(); b.isValid(); b = b.next())
            if (b.text().startsWith(QStringLiteral("@@ -2,7"))) hunkLine = b;
        QVERIFY(hunkLine.isValid());
        const QRect cr = diff->cursorRect(QTextCursor(hunkLine));
        QTest::mouseClick(diff->viewport(), Qt::LeftButton, {}, QPoint(diff->viewport()->width() - 20, cr.center().y()));
        QTRY_COMPARE(r.git({"show", ":f.txt"}).count(QStringLiteral("five")), 1);
        QVERIFY(!r.git({"show", ":f.txt"}).contains(QStringLiteral("thirty")));
        // The staged changes row appears; the unstaged one stays selected and loses the hunk.
        QTRY_COMPARE(model(&w)->rowCount(), 3);
        QTRY_VERIFY(model(&w)->commitAt(model(&w)->rowOf(Oid::workdirId())) && m_selectedIsWorkdir(&w));
        QTRY_VERIFY(!diffText(&w).contains(QStringLiteral("+five")));
        QVERIFY(diffText(&w).contains(QStringLiteral("+thirty")));

        // Stage the new file from its header button.
        QMetaObject::invokeMethod(diff, "fileActionRequested", Q_ARG(QString, QStringLiteral("new.txt")),
                                  Q_ARG(int, DiffView::StageAction));
        QTRY_VERIFY(r.git({"status", "--porcelain"}).contains(QStringLiteral("A  new.txt")));

        // The staged row offers Unstage.
        list->setCurrentIndex(model(&w)->index(model(&w)->rowOf(Oid::indexId()), 0));
        QTRY_VERIFY(diffFiles(&w).contains(QStringLiteral("new.txt")));
        QTRY_COMPARE(diff->actionSet(), DiffView::IndexActions);
        QCOMPARE(diff->fileActions(QStringLiteral("new.txt")), int(DiffView::UnstageAction));
        shot(&w, QStringLiteral("21-staged-changes"));

        // Writing the message doesn't trigger the window's single-key shortcuts.
        auto *edit = child<QPlainTextEdit>(&w, "commitMessage");
        auto *button = child<QPushButton>(&w, "commitButton");
        QVERIFY(edit->isVisible());
        QTRY_VERIFY(child<QLabel>(&w, "commitInfo")->text().contains(QStringLiteral("2 files staged")));
        QVERIFY(!button->isEnabled());   // no message yet
        edit->setFocus();
        const QString selectedBefore = selectedId(&w);
        QTest::keyClicks(edit, QStringLiteral("pnj Add five and new"));
        QCOMPARE(edit->toPlainText(), QStringLiteral("pnj Add five and new"));
        QCOMPARE(selectedId(&w), selectedBefore);
        QVERIFY(button->isEnabled());

        // The draft survives until the commit.
        QTRY_VERIFY(QFile::exists(QDir(r.path()).filePath(QStringLiteral(".git/gitq/COMMIT_DRAFT"))));
        QTest::keyClick(edit, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE(r.git({"log", "-1", "--format=%s"}).trimmed(), QStringLiteral("pnj Add five and new"));
        QTRY_COMPARE(selectedId(&w), r.rev("HEAD"));   // the new commit is shown
        QVERIFY(edit->toPlainText().isEmpty());
        QVERIFY(!QFile::exists(QDir(r.path()).filePath(QStringLiteral(".git/gitq/COMMIT_DRAFT"))));
        QTRY_COMPARE(model(&w)->rowCount(), 3);   // local changes (thirty) + 2 commits
        QVERIFY(model(&w)->commitAt(0)->id.isWorkdir());

        // Amend shows HEAD's message for editing.
        auto *amend = child<QCheckBox>(&w, "amendCheck");
        amend->setChecked(true);
        QCOMPARE(edit->toPlainText(), QStringLiteral("pnj Add five and new"));
        QVERIFY(button->isEnabled());   // amending needs nothing staged
        edit->setPlainText(QStringLiteral("Add five and new"));
        button->click();
        QTRY_VERIFY(!amend->isChecked());   // reset once the commit is done
        QCOMPARE(r.git({"log", "-1", "--format=%s"}).trimmed(), QStringLiteral("Add five and new"));
        QCOMPARE(r.git({"rev-list", "--count", "HEAD"}).trimmed(), QStringLiteral("2"));
        QTRY_COMPARE(selectedId(&w), r.rev("HEAD"));
    }

    void stageHunkShowsNextHunk() {
        // Files with long hunks, so that the diff scrolls.
        auto numbered = [](int n) {
            QByteArray t;
            for (int i = 1; i <= n; ++i) t += QByteArray::number(i) + '\n';
            return t;
        };
        auto changed = [](QByteArray t, const QList<int> &starts, const QByteArray &tag) {
            for (int s : starts)
                for (int i = s; i < s + 15; ++i)
                    t.replace('\n' + QByteArray::number(i) + '\n', '\n' + tag + QByteArray::number(i) + '\n');
            return t;
        };
        test::TestRepo r;
        r.commit("Base", {{"f.txt", numbered(200)}, {"g.txt", numbered(400)}});
        r.write("f.txt", changed(numbered(200), {5, 70, 140}, "f"));
        r.write("g.txt", changed(numbered(400), {5, 70, 140, 210, 280, 350}, "g"));
        const QString f = QStringLiteral("f.txt"), g = QStringLiteral("g.txt");

        MainWindow w(r.path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        auto *list = child<QTreeView>(&w, "commitList");
        list->setCurrentIndex(model(&w)->index(0, 0));
        auto *diff = child<DiffView>(&w, "diffView");
        QTRY_COMPARE(diff->hunkCount(g), 6);
        QCOMPARE(diff->hunkCount(f), 3);

        // Stage f.txt's second hunk: the index row appears (a reload), its third hunk takes its place.
        QVERIFY(diff->scrollToHunk(f, 1, 10));
        QCOMPARE(diff->hunkLineOffset(f, 1).value_or(-999), 10);
        QMetaObject::invokeMethod(diff, "hunkActionRequested", Q_ARG(QString, f), Q_ARG(int, 1),
                                  Q_ARG(int, DiffView::StageAction));
        QTRY_COMPARE(model(&w)->rowCount(), 3);
        QTRY_COMPARE(diff->hunkCount(f), 2);
        QVERIFY(m_selectedIsWorkdir(&w));
        QVERIFY(!diffText(&w).contains(QStringLiteral("+f75")));
        QTRY_COMPARE(diff->hunkLineOffset(f, 1).value_or(-999), 10);
        QVERIFY(diffText(&w).contains(QStringLiteral("+f145")));
        QVERIFY(diff->verticalScrollBar()->value() > 0);

        // Again (a refresh of the diff shown): f.txt's last hunk, so g.txt's first one comes next.
        QMetaObject::invokeMethod(diff, "hunkActionRequested", Q_ARG(QString, f), Q_ARG(int, 1),
                                  Q_ARG(int, DiffView::StageAction));
        QTRY_COMPARE(diff->hunkCount(f), 1);
        QTRY_COMPARE(diff->hunkLineOffset(g, 0).value_or(-999), 10);
    }

    void onlySelectedFiles() {
        QByteArray text;
        for (int i = 1; i <= 60; ++i) text += QByteArray::number(i) + '\n';
        QByteArray twoHunks = text;
        twoHunks.replace("\n5\n", "\nfive\n").replace("\n40\n", "\nforty\n");
        test::TestRepo r;
        r.commit("Base", {{"a.txt", text}, {"b.txt", text}, {"c.txt", text}});
        r.write("a.txt", text + "a\n");
        r.write("b.txt", twoHunks);
        r.write("c.txt", text + "c\n");
        const QString a = QStringLiteral("a.txt"), b = QStringLiteral("b.txt"), c = QStringLiteral("c.txt");

        MainWindow w(r.path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        child<QTreeView>(&w, "commitList")->setCurrentIndex(model(&w)->index(0, 0));
        QTRY_COMPARE(diffFiles(&w), QStringList({a, b, c}));
        auto *diff = child<DiffView>(&w, "diffView");
        auto *files = child<QListView>(&w, "fileList");
        auto *fileModel = qobject_cast<FileListModel *>(files->model());
        auto selectedPaths = [&] {
            QStringList out;
            for (const QModelIndex &i : files->selectionModel()->selectedIndexes()) out << i.data(FileListModel::PathRole).toString();
            return out;
        };

        // Nothing selected: everything. Select b.txt: only it.
        child<QCheckBox>(&w, "onlySelectedFiles")->setChecked(true);
        QCOMPARE(diffFiles(&w), QStringList({a, b, c}));
        files->selectionModel()->setCurrentIndex(fileModel->index(fileModel->rowOfPath(b)), QItemSelectionModel::ClearAndSelect);
        QCOMPARE(diffFiles(&w), QStringList({b}));

        // Staging one of its hunks keeps it shown (the index row appears: a reload).
        QMetaObject::invokeMethod(diff, "hunkActionRequested", Q_ARG(QString, b), Q_ARG(int, 0), Q_ARG(int, DiffView::StageAction));
        QTRY_COMPARE(model(&w)->rowCount(), 3);
        QTRY_COMPARE(diff->hunkCount(b), 1);
        QCOMPARE(diffFiles(&w), QStringList({b}));
        QCOMPARE(selectedPaths(), QStringList({b}));

        // Staging its last hunk: the next file is shown and selected.
        QMetaObject::invokeMethod(diff, "hunkActionRequested", Q_ARG(QString, b), Q_ARG(int, 0), Q_ARG(int, DiffView::StageAction));
        QTRY_COMPARE(diffFiles(&w), QStringList({c}));
        QCOMPARE(selectedPaths(), QStringList({c}));

        // No next file: the one before.
        QMetaObject::invokeMethod(diff, "fileActionRequested", Q_ARG(QString, c), Q_ARG(int, DiffView::StageAction));
        QTRY_COMPARE(diffFiles(&w), QStringList({a}));
        QCOMPARE(selectedPaths(), QStringList({a}));

        // Unchecked: all files again.
        r.write("c.txt", text + "c\nmore\n");
        child<QCheckBox>(&w, "onlySelectedFiles")->setChecked(false);
        QTRY_COMPARE(diffFiles(&w), QStringList({a, c}));
        Settings::instance().onlySelectedFiles = false;
    }

    void commitPaneDocked() {
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        auto *commitPane = child<ads::CDockWidget>(&w, "commitPane");
        QVERIFY(commitPane->dockAreaWidget() != child<ads::CDockWidget>(&w, "diffPane")->dockAreaWidget());
        QVERIFY(commitPane->dockAreaWidget() != child<ads::CDockWidget>(&w, "filesPane")->dockAreaWidget());
        QVERIFY(child<QPlainTextEdit>(&w, "commitMessage")->isVisible());
        QVERIFY(child<QLabel>(&w, "commitBranch")->text().contains(QStringLiteral("master")));
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
        w.resize(480, 900);   // commit ids get shortened
        QTest::qWait(100);
        shot(&w, QStringLiteral("06b-multiline-narrow"));
        w.resize(1200, 900);

        toggle->trigger();
        QVERIFY(!Settings::instance().commitListMultiline);
        QCOMPARE(list->sizeHintForRow(0), singleHeight);
        QVERIFY(!list->header()->isSectionHidden(CommitModel::AuthorColumn));
        QVERIFY(!list->header()->isSectionHidden(CommitModel::DateColumn));
        QVERIFY(list->header()->isVisible());
        QVERIFY(list->header()->sectionSize(CommitModel::AuthorColumn) > 0);
    }

    void fileHeaders() {
        test::TestRepo r;
        QByteArray big;
        for (int i = 0; i < 200; ++i) big += "line " + QByteArray::number(i) + "\n";
        r.commit("Two files", {{"a/long.txt", big}, {"b.txt", "b\n"}});
        MainWindow w(r.path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        auto *diff = child<DiffView>(&w, "diffView");
        QTRY_COMPARE(diffFiles(&w), (QStringList{"a/long.txt", "b.txt"}));
        // The scroll bar marks the added lines (most of this diff) in green.
        const QImage marks = diff->verticalScrollBar()->grab().toImage();
        const QColor mid = marks.pixelColor(marks.width() / 2, marks.height() / 2);
        QVERIFY2(mid.green() > mid.red() + 40, qPrintable(mid.name()));
        const QColor bar = Settings::instance().fileSepBgColor;
        auto topPixel = [&] { return diff->viewport()->grab().toImage().pixelColor(diff->viewport()->width() / 2, 2); };
        QVERIFY(topPixel() != bar);   // the header card is at the top
        // Scrolled into the middle of the first file, its header stays at the top.
        QVERIFY(diff->scrollToLine("a/long.txt", 100, true));
        QTRY_COMPARE(diff->fileAtTop(), QStringLiteral("a/long.txt"));
        QCOMPARE(topPixel(), bar);
        shot(&w, QStringLiteral("10-sticky-header"));
        // A click on it goes back to the top of the file.
        QTest::mouseClick(diff->viewport(), Qt::LeftButton, {}, QPoint(diff->viewport()->width() / 2, 4));
        QTRY_VERIFY(diff->verticalScrollBar()->value() < 20);
    }

    void sideBySide() {
        MainWindow w(repo->path(), {});
        w.resize(1200, 900);
        w.show();
        waitLoaded(&w);
        selectId(&w, fixId);
        auto *diff = child<DiffView>(&w, "diffView");
        child<QToolButton>(&w, "viewSplit")->click();
        QVERIFY(Settings::instance().sideBySide);
        // The old and new versions of the changed line share a row.
        auto row = [&] {
            for (const QString &l : diffText(&w).split(QLatin1Char('\n')))
                if (l.contains(QStringLiteral("int x = 1;")) && l.contains(QStringLiteral("int x = 3;"))) return l;
            return QString();
        };
        QTRY_VERIFY(!row().isEmpty());
        QTest::qWait(300);   // a re-layout for the final width, if any
        QCOMPARE(diff->horizontalScrollBar()->maximum(), 0);
        QVERIFY(row().indexOf(QStringLiteral("int x = 3;")) > row().indexOf(QStringLiteral("int x = 1;")));
        // Each side's line: right-clicking the left half gives the old line, the right half the new one.
        const QTextBlock block = diff->document()->find(QStringLiteral("int x = 1;")).block();
        const QRect r = diff->cursorRect(QTextCursor(block));
        const DiffLineInfo left = diff->lineInfoAt(QPoint(r.left() + 20, r.center().y()));
        QCOMPARE(left.kind, QLatin1Char('-'));
        QCOMPARE(left.oldLine, 2);
        const DiffLineInfo right = diff->lineInfoAt(QPoint(diff->viewport()->width() - 20, r.center().y()));
        QCOMPARE(right.kind, QLatin1Char('+'));
        QCOMPARE(right.newLine, 2);
        shot(&w, QStringLiteral("11-side-by-side"));
        // Narrower: the rows are laid out again for the new width.
        w.resize(900, 900);
        QTRY_COMPARE(diff->horizontalScrollBar()->maximum(), 0);
        child<QToolButton>(&w, "viewUnified")->click();
        QTRY_VERIFY(diffText(&w).contains(QStringLiteral("+int x = 3;")));
        QVERIFY(!Settings::instance().sideBySide);
    }

    void changedWords() {
        QList<DiffView::WordRange> o, n;
        const double sim = DiffView::changedWords("int x = 1;", "int x = 23;", 1, &o, &n);
        QVERIFY(sim > 0.5);
        QCOMPARE(o.size(), 1);
        QCOMPARE(n.size(), 1);
        QCOMPARE(o[0].start, 9);   // after the mark and "int x = "
        QCOMPARE(o[0].len, 1);
        QCOMPARE(n[0].len, 2);
        QVERIFY(DiffView::changedWords("import os", "def greet(name):", 1, &o, &n) < 0.4);
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
            QString before = selectedId(&w);
            QTest::keyClick(diff, Qt::Key_N);
            QTRY_VERIFY(selectedId(&w) != before);

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
