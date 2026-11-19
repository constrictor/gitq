#include "TestRepo.h"
#include "git/DiffEngine.h"
#include "git/LogLoader.h"

#include <QTemporaryDir>

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
// Superproject `r` with the submodule `sub` at lib/sub: "add sub" records s1, "bump sub" moves
// it to s3 (s2 changes x.c, s3 adds z.c) and changes main.c.
struct SubmoduleFixture {
    test::TestRepo sub, r;
    QString s1, s2, s3, addSub, bump;
    SubmoduleFixture() {
        s1 = sub.commit("s1", {{"x.c", "1\n2\n3\n"}, {"y.c", "y\n"}});
        r.commit("init", {{"main.c", "m\n"}});
        r.addSubmodule(sub.path(), "lib/sub");
        addSub = r.commit("add sub");
        s2 = sub.commit("s2", {{"x.c", "1\ntwo\n3\n"}});
        s3 = sub.commit("s3", {{"z.c", "z\n"}});
        r.moveSubmodule("lib/sub", s3);
        bump = r.commit("bump sub", {{"main.c", "m2\n"}});
    }
};

QStringList paths(const DiffResult &d) {
    QStringList out;
    for (const FileDiff &f : d.files) out << f.path();
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

    void submoduleSummary() {
        SubmoduleFixture fx;
        Repository repo = fx.r.open();
        DiffResult d = DiffEngine::diffCommit(repo, Oid::fromHex(fx.bump), DiffOptions());
        QCOMPARE(paths(d), (QStringList{"lib/sub", "main.c"}));
        QCOMPARE(d.files[0].submodule, 0);
        QCOMPARE(d.submodules.size(), 1);
        const SubmoduleChange &s = d.submodules[0];
        QCOMPARE(s.oldId, Oid::fromHex(fx.s1));
        QCOMPARE(s.newId, Oid::fromHex(fx.s3));
        QVERIFY(!s.detailed);
        QVERIFY(s.added.isEmpty());
        QVERIFY(d.files[0].lines.contains("+Subproject commit " + fx.s3));
    }

    void submoduleLog() {
        SubmoduleFixture fx;
        Repository repo = fx.r.open();
        DiffOptions o;
        o.submodules = DiffOptions::SubmoduleLog;
        DiffResult d = DiffEngine::diffCommit(repo, Oid::fromHex(fx.bump), o);
        QCOMPARE(paths(d), (QStringList{"lib/sub", "main.c"}));
        const SubmoduleChange &s = d.submodules[0];
        QVERIFY(s.detailed);
        QVERIFY(!s.expanded);
        QVERIFY(s.expandable);
        QVERIFY(s.notes.isEmpty());
        QCOMPARE(s.status, QChar('M'));
        QVERIFY(!s.repoPath.isEmpty());
        QCOMPARE(s.added, (QList<CommitLink>{{Oid::fromHex(fx.s3), "s3"}, {Oid::fromHex(fx.s2), "s2"}}));
        QVERIFY(s.removed.isEmpty());

        // Expanding it adds its file changes, as paths in the superproject.
        o.expandSubmodules = {"lib/sub"};
        d = DiffEngine::diffCommit(repo, Oid::fromHex(fx.bump), o);
        QCOMPARE(paths(d), (QStringList{"lib/sub", "lib/sub/x.c", "lib/sub/z.c", "main.c"}));
        QVERIFY(d.submodules[0].expanded);
        QCOMPARE(d.files[1].inSubmodule, QString("lib/sub"));
        QCOMPARE(d.files[2].status, QChar('A'));
        QVERIFY(d.files[3].inSubmodule.isEmpty());
        QVERIFY(d.files[1].lines.contains("--- a/lib/sub/x.c"));
        QStringList want = hunks(fx.sub.git({"diff", "--no-color", fx.s1, fx.s3, "--", "x.c"}).split('\n', Qt::SkipEmptyParts));
        QCOMPARE(hunks(d.files[1].lines), want);
        QCOMPARE(d.submoduleFor("lib/sub/x.c")->path, QString("lib/sub"));
        QCOMPARE(d.submoduleFor("lib/sub")->path, QString("lib/sub"));
        QVERIFY(!d.submoduleFor("main.c"));

        // Full diff mode expands every submodule; path limits apply inside it.
        o.expandSubmodules.clear();
        o.submodules = DiffOptions::SubmoduleDiff;
        d = DiffEngine::diffCommit(repo, Oid::fromHex(fx.bump), o);
        QCOMPARE(paths(d), (QStringList{"lib/sub", "lib/sub/x.c", "lib/sub/z.c", "main.c"}));
        o.paths = {"lib/sub/x.c"};
        d = DiffEngine::diffCommit(repo, Oid::fromHex(fx.bump), o);
        QCOMPARE(paths(d), (QStringList{"lib/sub", "lib/sub/x.c"}));

        // Moving it back is a rewind.
        fx.r.moveSubmodule("lib/sub", fx.s2);
        QString rewind = fx.r.commit("rewind sub");
        o = DiffOptions();
        o.submodules = DiffOptions::SubmoduleLog;
        d = DiffEngine::diffCommit(repo, Oid::fromHex(rewind), o);
        QVERIFY(d.submodules[0].added.isEmpty());
        QCOMPARE(d.submodules[0].removed, (QList<CommitLink>{{Oid::fromHex(fx.s3), "s3"}}));

        // Adding a submodule lists no commits and is not expanded.
        o.submodules = DiffOptions::SubmoduleDiff;
        d = DiffEngine::diffCommit(repo, Oid::fromHex(fx.addSub), o);
        QCOMPARE(paths(d), (QStringList{".gitmodules", "lib/sub"}));
        QCOMPARE(d.submodules[0].status, QChar('A'));
        QVERIFY(d.submodules[0].oldId.isNull());
        QVERIFY(!d.submodules[0].expandable);
        QVERIFY(!d.submodules[0].expanded);
    }

    void submoduleLocalChanges() {
        SubmoduleFixture fx;
        Repository repo = fx.r.open();
        bool index = false, workdir = false;
        repo.localChanges(&index, &workdir);
        QVERIFY(!index && !workdir);
        fx.r.write("lib/sub/y.c", "y changed\n");
        repo.localChanges(&index, &workdir);
        QVERIFY(workdir);
        DiffOptions o;
        o.submodules = DiffOptions::SubmoduleDiff;
        DiffResult d = DiffEngine::diffCommit(repo, Oid::workdirId(), o);
        QCOMPARE(paths(d), (QStringList{"lib/sub", "lib/sub/y.c"}));
        QVERIFY(d.submodules[0].newIsWorkdir);
        QVERIFY(d.submodules[0].notes.contains("modified content"));
        QVERIFY(d.files[1].lines.contains("+y changed"));
    }

    void submoduleNotCheckedOut() {
        SubmoduleFixture fx;
        QTemporaryDir dir;
        const QString clone = dir.filePath("clone");
        fx.r.git({"clone", "-q", fx.r.path(), clone});
        Repository repo = Repository::open(clone);
        DiffOptions o;
        o.submodules = DiffOptions::SubmoduleDiff;
        DiffResult d = DiffEngine::diffCommit(repo, Oid::fromHex(fx.bump), o);
        QCOMPARE(paths(d), (QStringList{"lib/sub", "main.c"}));
        QVERIFY(d.submodules[0].repoPath.isEmpty());
        QVERIFY(d.submodules[0].notes.contains("submodule not checked out"));
        QVERIFY(!d.submodules[0].expandable);
    }

    void submoduleMerge() {
        SubmoduleFixture fx;
        QString s4 = fx.sub.commit("s4", {{"w.c", "w\n"}});
        fx.r.git({"checkout", "-q", "-b", "side", fx.addSub});
        fx.r.git({"-C", "lib/sub", "checkout", "-q", fx.s2});
        fx.r.commit("side moves sub");
        fx.r.git({"checkout", "-q", "master"});
        fx.r.git({"-C", "lib/sub", "checkout", "-q", fx.s3});
        fx.r.git({"merge", "-q", "-s", "ours", "--no-commit", "side"});
        fx.r.moveSubmodule("lib/sub", s4);
        QString merge = fx.r.commit("merge side");
        Repository repo = fx.r.open();
        DiffOptions o;
        QVERIFY(DiffEngine::diffCommit(repo, Oid::fromHex(merge), o).files.isEmpty());
        o.submodules = DiffOptions::SubmoduleDiff;
        DiffResult d = DiffEngine::diffCommit(repo, Oid::fromHex(merge), o);
        QCOMPARE(paths(d), (QStringList{"lib/sub"}));
        QCOMPARE(d.files[0].numParents, 2);
        QCOMPARE(d.submodules[0].oldId, Oid::fromHex(fx.s3));
        QCOMPARE(d.submodules[0].added, (QList<CommitLink>{{Oid::fromHex(s4), "s4"}}));
        QVERIFY(!d.submodules[0].expandable);
        QVERIFY(!d.submodules[0].expanded);
    }

    void submoduleTreeAndContents() {
        SubmoduleFixture fx;
        Repository repo = fx.r.open();
        const Oid bump = Oid::fromHex(fx.bump);
        QCOMPARE(DiffEngine::treeFiles(repo, bump), (QStringList{".gitmodules", "lib/sub", "main.c"}));
        QCOMPARE(DiffEngine::treeFiles(repo, bump, true),
                 (QStringList{".gitmodules", "lib/sub/x.c", "lib/sub/y.c", "lib/sub/z.c", "main.c"}));
        QCOMPARE(DiffEngine::treeFiles(repo, Oid::indexId(), true),
                 (QStringList{".gitmodules", "lib/sub/x.c", "lib/sub/y.c", "lib/sub/z.c", "main.c"}));
        bool found = false;
        QCOMPARE(DiffEngine::fileContents(repo, bump, "lib/sub/x.c", &found), QByteArray("1\ntwo\n3\n"));
        QVERIFY(found);
        QCOMPARE(DiffEngine::fileContents(repo, Oid::fromHex(fx.addSub), "lib/sub/x.c"), QByteArray("1\n2\n3\n"));
        QCOMPARE(DiffEngine::fileContents(repo, Oid::indexId(), "lib/sub/z.c"), QByteArray("z\n"));
        DiffEngine::fileContents(repo, Oid::fromHex(fx.addSub), "lib/sub/z.c", &found);
        QVERIFY(!found);

        SubmoduleLocation loc;
        QVERIFY(DiffEngine::locateInSubmodule(repo, bump, "lib/sub/x.c", &loc));
        QCOMPARE(loc.prefix, QString("lib/sub"));
        QCOMPARE(loc.path, QString("x.c"));
        QCOMPARE(loc.commit, Oid::fromHex(fx.s3));
        QCOMPARE(Repository::open(loc.repoPath).resolveCommit("HEAD"), Oid::fromHex(fx.s3));
        QVERIFY(DiffEngine::locateInSubmodule(repo, Oid::fromHex(fx.addSub), "lib/sub", &loc));
        QCOMPARE(loc.commit, Oid::fromHex(fx.s1));
        QVERIFY(loc.path.isEmpty());
        QVERIFY(DiffEngine::locateInSubmodule(repo, Oid::workdirId(), "lib/sub/x.c", &loc));
        QVERIFY(loc.commit.isWorkdir());
        QVERIFY(!DiffEngine::locateInSubmodule(repo, bump, "main.c", &loc));
    }

    void submodulePathFilter() {
        SubmoduleFixture fx;
        Repository repo = fx.r.open();
        auto tree = [&](const QString &rev) {
            git_tree *t = nullptr;
            CommitPtr c = repo.lookupCommit(Oid::fromHex(fx.r.rev(rev)));
            git_commit_tree(&t, c.get());
            return TreePtr(t);
        };
        TreePtr bump = tree(fx.bump), before = tree(fx.addSub), init = tree("HEAD~2");
        PathFilter inside(repo.raw(), {"lib/sub/x.c"});
        QVERIFY(!inside.treeSame(bump.get(), before.get()));   // the commit moving the submodule
        QVERIFY(!inside.treeSame(before.get(), init.get()));   // the commit adding it
        PathFilter other(repo.raw(), {"main.c"});
        QVERIFY(other.treeSame(before.get(), init.get()));
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
