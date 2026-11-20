#include "TestRepo.h"
#include "git/DiffEngine.h"
#include "git/Staging.h"

#include <QFile>

using namespace gitq;

namespace {

QByteArray numbered(int n, const QMap<int, QByteArray> &changes = {}) {
    QByteArray out;
    for (int i = 1; i <= n; ++i) out += changes.value(i, QByteArray::number(i)) + '\n';
    return out;
}

// The hunk `index` of `path` as the diff pane shows it (local changes or staged changes row).
HunkRef hunkOf(const Repository &repo, bool staged, const QString &path, int index, int context = 3) {
    DiffOptions o;
    o.context = context;
    o.includeUntracked = true;
    DiffResult d = DiffEngine::diffCommit(repo, staged ? Oid::indexId() : Oid::workdirId(), o);
    for (const FileDiff &f : d.files) {
        if (f.path() != path) continue;
        const QList<FileDiff::Hunk> hunks = f.hunks();
        if (index >= hunks.size()) break;
        HunkRef h;
        h.path = path;
        h.context = context;
        h.header = f.lines[hunks[index].start];
        h.body = f.lines.mid(hunks[index].start + 1, hunks[index].end - hunks[index].start - 1);
        return h;
    }
    qFatal("no hunk %d in %s", index, qPrintable(path));
}

QByteArray readFile(test::TestRepo &r, const QString &f) {
    QFile file(QDir(r.path()).filePath(f));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestStaging : public QObject {
    Q_OBJECT
    LibGit2Init init;
private slots:
    void stageAndUnstageFiles() {
        test::TestRepo r;
        r.commit("a", {{"m.txt", "1\n"}, {"d.txt", "d\n"}});
        r.write("m.txt", "2\n");
        r.write("new.txt", "n\n");
        QFile::remove(QDir(r.path()).filePath("d.txt"));
        Repository repo = r.open();
        OpResult res = Ops::stageFiles(repo, {"m.txt", "new.txt", "d.txt"});
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"status", "--porcelain"}), QString("D  d.txt\nM  m.txt\nA  new.txt\n"));
        StagedSummary s = Ops::stagedSummary(repo);
        QCOMPARE(s.files, 3);
        QCOMPARE(s.added, 2);
        QCOMPARE(s.removed, 2);

        res = Ops::unstageFiles(repo, {"m.txt", "new.txt", "d.txt"});
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"status", "--porcelain"}), QString(" D d.txt\n M m.txt\n?? new.txt\n"));
    }

    void unstageOnUnbornBranch() {
        test::TestRepo r;
        r.write("a.txt", "a\n");
        r.git({"add", "a.txt"});
        Repository repo = r.open();
        OpResult res = Ops::unstageFiles(repo, {"a.txt"});
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"status", "--porcelain"}), QString("?? a.txt\n"));
    }

    void unstageRename() {
        test::TestRepo r;
        r.commit("a", {{"old.txt", numbered(20)}});
        r.git({"mv", "old.txt", "new.txt"});
        Repository repo = r.open();
        OpResult res = Ops::unstageFiles(repo, {"old.txt", "new.txt"});
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"status", "--porcelain"}), QString(" D old.txt\n?? new.txt\n"));
    }

    void pathspecIsLiteral() {
        test::TestRepo r;
        r.commit("a", {{"x.txt", "x\n"}});
        r.write("*.txt", "star\n");
        r.write("x.txt", "changed\n");
        Repository repo = r.open();
        QVERIFY(Ops::stageFiles(repo, {"*.txt"}).ok);
        QCOMPARE(r.git({"status", "--porcelain"}), QString("A  *.txt\n M x.txt\n"));
        QVERIFY(!Ops::stageFiles(repo, {"../outside"}).ok);
    }

    void discardAndRestore() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", "1\n"}});
        r.write("f.txt", "local\n");
        Repository repo = r.open();
        QList<DiscardBackup> backups;
        OpResult res = Ops::discardFiles(repo, {"f.txt"}, &backups);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(readFile(r, "f.txt"), QByteArray("1\n"));
        QCOMPARE(backups.size(), 1);
        QVERIFY(!backups[0].untracked);
        QVERIFY(!Ops::backupIsCurrent(repo, backups[0]));
        QVERIFY(QFile::exists(QDir(repo.path()).filePath("gitq/discarded")));

        res = Ops::restoreDiscarded(repo, backups[0]);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(readFile(r, "f.txt"), QByteArray("local\n"));
        QVERIFY(Ops::backupIsCurrent(repo, backups[0]));
    }

    void deleteUntrackedAndRestore() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", "1\n"}});
        r.write("dir/sub/n.txt", "new\n");
        r.write("n2.txt", "new2\n");
        QFile::setPermissions(QDir(r.path()).filePath("n2.txt"), QFile::permissions(QDir(r.path()).filePath("n2.txt")) | QFile::ExeOwner);
        Repository repo = r.open();
        QList<DiscardBackup> backups;
        OpResult res = Ops::deleteUntracked(repo, {"dir/sub/n.txt", "n2.txt"}, &backups);
        QVERIFY2(res.ok, qPrintable(res.message));
        QVERIFY(!QFile::exists(QDir(r.path()).filePath("n2.txt")));
        QVERIFY(!QDir(QDir(r.path()).filePath("dir")).exists());   // emptied directories are gone
        QCOMPARE(r.git({"status", "--porcelain"}), QString());
        QCOMPARE(backups.size(), 2);
        for (const DiscardBackup &b : backups) QVERIFY2(Ops::restoreDiscarded(repo, b).ok, qPrintable(b.path));
        QCOMPARE(readFile(r, "dir/sub/n.txt"), QByteArray("new\n"));
        QVERIFY(QFile::permissions(QDir(r.path()).filePath("n2.txt")) & QFile::ExeOwner);
    }

    void splitPatch() {
        const QByteArray p = "diff --git a/f b/f\n--- a/f\n+++ b/f\n@@ -1 +1 @@\n-a\n+b\n@@ -9 +9 @@ ctx\n-c\n+d\n"
                             "\\ No newline at end of file\n";
        SplitPatch s = Ops::splitPatch(p);
        QCOMPARE(s.header, QByteArray("diff --git a/f b/f\n--- a/f\n+++ b/f\n"));
        QCOMPARE(s.hunks.size(), 2);
        QCOMPARE(s.hunks[0], QByteArray("@@ -1 +1 @@\n-a\n+b\n"));
        QVERIFY(s.hunks[1].endsWith("\\ No newline at end of file\n"));
        QCOMPARE(Ops::hunkRange("@@ -9 +9 @@ ctx"), QString("@@ -9 +9 @@"));
    }

    void stageUnstageDiscardHunks() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", numbered(60)}});
        r.write("f.txt", numbered(60, {{5, "five"}, {30, "thirty"}, {55, "fifty-five"}}));
        Repository repo = r.open();

        // Stage the middle hunk: only it is in the index.
        OpResult res = Ops::applyHunk(repo, hunkOf(repo, false, "f.txt", 1), Ops::StageHunk);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"show", ":f.txt"}).toUtf8(), numbered(60, {{30, "thirty"}}));
        QCOMPARE(readFile(r, "f.txt"), numbered(60, {{5, "five"}, {30, "thirty"}, {55, "fifty-five"}}));

        // Stage the last one too, then unstage the middle one from the staged view.
        QVERIFY(Ops::applyHunk(repo, hunkOf(repo, false, "f.txt", 1), Ops::StageHunk).ok);
        QCOMPARE(r.git({"show", ":f.txt"}).toUtf8(), numbered(60, {{30, "thirty"}, {55, "fifty-five"}}));
        res = Ops::applyHunk(repo, hunkOf(repo, true, "f.txt", 0), Ops::UnstageHunk);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"show", ":f.txt"}).toUtf8(), numbered(60, {{55, "fifty-five"}}));

        // Discard the first hunk from the work tree; the backup holds the whole file.
        QList<DiscardBackup> backups;
        res = Ops::applyHunk(repo, hunkOf(repo, false, "f.txt", 0), Ops::DiscardHunk, &backups);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(readFile(r, "f.txt"), numbered(60, {{30, "thirty"}, {55, "fifty-five"}}));
        QCOMPARE(backups.size(), 1);
        QVERIFY(Ops::restoreDiscarded(repo, backups[0]).ok);
        QCOMPARE(readFile(r, "f.txt"), numbered(60, {{5, "five"}, {30, "thirty"}, {55, "fifty-five"}}));
    }

    void hunkWithZeroContext() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", numbered(10)}});
        r.write("f.txt", numbered(10, {{2, "two"}, {4, "four"}}));
        Repository repo = r.open();
        OpResult res = Ops::applyHunk(repo, hunkOf(repo, false, "f.txt", 1, 0), Ops::StageHunk);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"show", ":f.txt"}).toUtf8(), numbered(10, {{4, "four"}}));
    }

    void hunkCrlfAndNoNewline() {
        test::TestRepo r;
        r.commit("a", {{"w.txt", "a\r\nb\r\nc\r\n"}, {"e.txt", "x\ny"}});
        r.write("w.txt", "a\r\nB\r\nc\r\n");
        r.write("e.txt", "x\nY");
        Repository repo = r.open();
        QVERIFY2(Ops::applyHunk(repo, hunkOf(repo, false, "w.txt", 0), Ops::StageHunk).ok, "crlf");
        QCOMPARE(r.git({"show", ":w.txt"}).toUtf8(), QByteArray("a\r\nB\r\nc\r\n"));
        OpResult res = Ops::applyHunk(repo, hunkOf(repo, false, "e.txt", 0), Ops::StageHunk);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"show", ":e.txt"}).toUtf8(), QByteArray("x\nY"));
    }

    void staleHunkIsRefused() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", numbered(10)}});
        r.write("f.txt", numbered(10, {{5, "five"}}));
        Repository repo = r.open();
        HunkRef shown = hunkOf(repo, false, "f.txt", 0);
        r.write("f.txt", numbered(10, {{5, "FIVE"}}));   // changed after it was shown
        OpResult res = Ops::applyHunk(repo, shown, Ops::StageHunk);
        QVERIFY(!res.ok);
        QVERIFY(res.message.contains("changed"));
        QCOMPARE(r.git({"diff", "--cached", "--stat"}), QString());
    }

    void commitAndAmend() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", "1\n"}});
        r.write("f.txt", "2\n");
        Repository repo = r.open();
        QVERIFY(Ops::stageFiles(repo, {"f.txt"}).ok);
        CommitRequest req;
        req.message = "Change f\n\nBody line.\n";
        req.signOff = true;
        OpResult res = Ops::commit(repo, req);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(res.newCommit.hex(), r.rev("HEAD"));
        QCOMPARE(r.git({"log", "-1", "--format=%B"}).trimmed(),
                 QString("Change f\n\nBody line.\n\nSigned-off-by: Tester <tester@example.com>"));
        QCOMPARE(Ops::headMessage(repo), QString("Change f\n\nBody line.\n\nSigned-off-by: Tester <tester@example.com>"));

        r.write("g.txt", "g\n");
        QVERIFY(Ops::stageFiles(repo, {"g.txt"}).ok);
        req = {};
        req.message = "Change f and add g";
        req.amend = true;
        res = Ops::commit(repo, req);
        QVERIFY2(res.ok, qPrintable(res.message));
        QCOMPARE(r.git({"rev-list", "--count", "HEAD"}).trimmed(), QString("2"));
        QCOMPARE(r.git({"log", "-1", "--format=%s"}).trimmed(), QString("Change f and add g"));
        QCOMPARE(r.git({"show", "--format=", "--name-only", "HEAD"}), QString("f.txt\ng.txt\n"));
    }

    void commitHookOutputIsReported() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", "1\n"}});
        const QString hook = QDir(r.path()).filePath(".git/hooks/commit-msg");
        QFile h(hook);
        QVERIFY(h.open(QIODevice::WriteOnly));
        h.write("#!/bin/sh\necho 'message must mention a ticket' >&2\nexit 1\n");
        h.close();
        QFile::setPermissions(hook, QFile::permissions(hook) | QFile::ExeOwner);
        r.write("f.txt", "2\n");
        Repository repo = r.open();
        QVERIFY(Ops::stageFiles(repo, {"f.txt"}).ok);
        CommitRequest req;
        req.message = "No ticket";
        OpResult res = Ops::commit(repo, req);
        QVERIFY(!res.ok);
        QVERIFY2(res.message.contains("ticket"), qPrintable(res.message));
        QCOMPARE(r.git({"rev-list", "--count", "HEAD"}).trimmed(), QString("1"));
    }

    void draftAndMergeMessage() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", "1\n"}});
        Repository repo = r.open();
        QCOMPARE(Ops::loadDraft(repo), QString());
        Ops::saveDraft(repo, "Work in progress\n");
        QCOMPARE(Ops::loadDraft(repo), QString("Work in progress\n"));
        Ops::saveDraft(repo, "  ");
        QCOMPARE(Ops::loadDraft(repo), QString());
        r.write(".git/MERGE_MSG", "Merge branch 'x'\n\n# Conflicts:\n#\tf.txt\n");
        QCOMPARE(Ops::mergeMessage(repo), QString("Merge branch 'x'"));
    }

    void untrackedInLocalChanges() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", "1\n"}, {".gitignore", "*.log\n"}});
        r.write("n/new.txt", "hello\n");
        r.write("skip.log", "ignored\n");
        Repository repo = r.open();
        bool idx = false, wd = false;
        repo.localChanges(&idx, &wd);
        QVERIFY(!wd);
        repo.localChanges(&idx, &wd, true);
        QVERIFY(wd);
        QVERIFY(DiffEngine::diffCommit(repo, Oid::workdirId(), DiffOptions()).files.isEmpty());
        DiffOptions o;
        o.includeUntracked = true;
        DiffResult d = DiffEngine::diffCommit(repo, Oid::workdirId(), o);
        QCOMPARE(d.files.size(), 1);
        QCOMPARE(d.files[0].path(), QString("n/new.txt"));
        QCOMPARE(d.files[0].status, QChar('?'));
        QVERIFY(d.files[0].oldPath.isEmpty());
        QVERIFY(d.files[0].lines.contains("+hello"));
        // Range diffs from a commit to the work tree are unaffected.
        QVERIFY(DiffEngine::diffBetween(repo, Oid::fromHex(r.rev("HEAD")), Oid::workdirId(), o).files.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestStaging)
#include "tst_staging.moc"
