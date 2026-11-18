#include "TestRepo.h"
#include "git/DiffEngine.h"

using namespace gitq;

namespace {
// Hunk lines (from the first "@@") of every file, to compare with git's output.
QStringList hunks(const QStringList &lines) {
    QStringList out;
    bool in = false;
    for (const QString &l : lines) {
        if (l.startsWith("diff ")) in = false;
        if (l.startsWith("@@")) in = true;
        if (in) out << l;
    }
    return out;
}

QStringList allHunks(const DiffResult &d) {
    QStringList out;
    for (const FileDiff &f : d.files) out << hunks(f.lines);
    return out;
}
} // namespace

class TestDiffEngine : public QObject {
    Q_OBJECT
    LibGit2Init init;
private slots:
    void simpleDiffMatchesGit() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n"}, {"g.txt", "g\n"}});
        QString b = r.commit("b", {{"f.txt", "1\n2\nthree\n4\n5\n6\n7\n8\nnine\n10\n"}, {"h.txt", "new\n"}});
        Repository repo = r.open();
        DiffResult d = DiffEngine::diffCommit(repo, Oid::fromHex(b), DiffOptions());
        QVERIFY(d.error.isEmpty());
        QCOMPARE(d.files.size(), 2);
        QCOMPARE(d.files[0].path(), QString("f.txt"));
        QCOMPARE(d.files[1].status, QChar('A'));
        QStringList want = hunks(r.git({"diff-tree", "-p", "--no-color", b}).split('\n', Qt::SkipEmptyParts));
        QCOMPARE(allHunks(d), want);

        DiffOptions o;
        o.context = 1;
        d = DiffEngine::diffCommit(repo, Oid::fromHex(b), o);
        want = hunks(r.git({"diff-tree", "-p", "-U1", "--no-color", b}).split('\n', Qt::SkipEmptyParts));
        QCOMPARE(allHunks(d), want);

        o.paths = {"h.txt"};
        d = DiffEngine::diffCommit(repo, Oid::fromHex(b), o);
        QCOMPARE(d.files.size(), 1);
    }

    void combinedDiffMatchesGit() {
        test::TestRepo r;
        r.commit("base", {{"f.txt", "a\nb\nc\nd\ne\n"}});
        r.git({"checkout", "-q", "-b", "side"});
        r.commit("side", {{"f.txt", "a\nB-side\nc\nd\ne\n"}});
        r.git({"checkout", "-q", "master"});
        r.commit("main", {{"f.txt", "a\nB-main\nc\nd\ne\n"}});
        r.git({"merge", "-q", "side"}, false);   // conflicts
        r.write("f.txt", "a\nB-resolved\nc\nd\ne\n");
        r.git({"add", "f.txt"});
        r.m_tick++;
        r.git({"commit", "-q", "-m", "merge"});
        QString m = r.rev("HEAD");
        Repository repo = r.open();
        DiffResult d = DiffEngine::diffCommit(repo, Oid::fromHex(m), DiffOptions());
        QCOMPARE(d.files.size(), 1);
        QCOMPARE(d.files[0].numParents, 2);
        QStringList want = hunks(r.git({"diff-tree", "--cc", "-p", "--no-color", m}).split('\n', Qt::SkipEmptyParts));
        QCOMPARE(hunks(d.files[0].lines), want);
    }

    void cleanMergeHasNoCombinedDiff() {
        test::TestRepo r;
        r.commit("base", {{"f.txt", "a\n"}, {"g.txt", "g\n"}});
        r.git({"checkout", "-q", "-b", "side"});
        r.commit("side", {{"f.txt", "a2\n"}});
        r.git({"checkout", "-q", "master"});
        r.commit("main", {{"g.txt", "g2\n"}});
        r.m_tick++;
        r.git({"merge", "-q", "--no-edit", "side"});
        Repository repo = r.open();
        DiffResult d = DiffEngine::diffCommit(repo, Oid::fromHex(r.rev("HEAD")), DiffOptions());
        QVERIFY(d.files.isEmpty());
    }

    void localChanges() {
        test::TestRepo r;
        r.commit("a", {{"f.txt", "1\n"}, {"g.txt", "g\n"}});
        r.write("f.txt", "2\n");
        r.write("g.txt", "staged\n");
        r.git({"add", "g.txt"});
        Repository repo = r.open();
        DiffResult wd = DiffEngine::diffCommit(repo, Oid::workdirId(), DiffOptions());
        QCOMPARE(wd.files.size(), 1);
        QCOMPARE(wd.files[0].path(), QString("f.txt"));
        DiffResult idx = DiffEngine::diffCommit(repo, Oid::indexId(), DiffOptions());
        QCOMPARE(idx.files.size(), 1);
        QCOMPARE(idx.files[0].path(), QString("g.txt"));
        DiffResult rev = DiffEngine::diffBetween(repo, Oid::indexId(), Oid::fromHex(r.rev("HEAD")), DiffOptions());
        QCOMPARE(rev.files.size(), 1);
        QVERIFY(rev.files[0].lines.contains("-staged"));
    }

    void treeAndContents() {
        test::TestRepo r;
        QString a = r.commit("a", {{"dir/x.txt", "x\n"}, {"y.txt", "y\n"}});
        Repository repo = r.open();
        QStringList files = DiffEngine::treeFiles(repo, Oid::fromHex(a));
        QCOMPARE(files, (QStringList{"dir/x.txt", "y.txt"}));
        QCOMPARE(DiffEngine::fileContents(repo, Oid::fromHex(a), "dir/x.txt"), QByteArray("x\n"));
    }

    void wordDiff() {
        auto segs = DiffEngine::wordDiff("int foo = 1;", "int bar = 1;");
        QCOMPARE(segs.size(), 4);
        QCOMPARE(segs[0].kind, WordSeg::Same);
        QCOMPARE(segs[1].kind, WordSeg::Removed);
        QCOMPARE(segs[1].text, QString("foo"));
        QCOMPARE(segs[2].kind, WordSeg::Added);
        QCOMPARE(segs[2].text, QString("bar"));
    }
};

QTEST_GUILESS_MAIN(TestDiffEngine)
#include "tst_diffengine.moc"
