#include "TestRepo.h"
#include "git/Ops.h"

using namespace gitq;

class TestOps : public QObject {
    Q_OBJECT
    LibGit2Init init;
private slots:
    void branchesAndTags() {
        test::TestRepo r;
        QString a = r.commit("a", {{"f", "1\n"}});
        r.commit("b", {{"f", "2\n"}});
        Repository repo = r.open();
        QVERIFY(Ops::createBranch(repo, "feature", Oid::fromHex(a), false).ok);
        QCOMPARE(r.rev("feature"), a);
        QVERIFY(!Ops::createBranch(repo, "bad..name", Oid::fromHex(a), false).ok);
        QVERIFY(Ops::renameBranch(repo, "feature", "feature2", false).ok);
        QCOMPARE(r.rev("feature2"), a);
        QVERIFY(Ops::createTag(repo, "v1", Oid::fromHex(a), QString()).ok);
        QVERIFY(Ops::createTag(repo, "v2", Oid::fromHex(a), "annotated").ok);
        QCOMPARE(r.git({"cat-file", "-t", "v2"}).trimmed(), QString("tag"));
        QCOMPARE(r.git({"cat-file", "-t", "v1"}).trimmed(), QString("commit"));
        QVERIFY(!Ops::deleteBranch(repo, "master").ok);   // checked out
        QVERIFY(Ops::checkoutBranch(repo, "feature2").ok);
        QCOMPARE(r.git({"symbolic-ref", "HEAD"}).trimmed(), QString("refs/heads/feature2"));
        QCOMPARE(r.git({"show", "HEAD:f"}), QString("1\n"));
        QVERIFY(Ops::deleteBranch(repo, "master").ok);
    }

    void cherryPickRevertReset() {
        test::TestRepo r;
        QString a = r.commit("a", {{"f", "1\n"}, {"g", "g\n"}});
        r.git({"checkout", "-q", "-b", "side"});
        QString s = r.commit("side change", {{"g", "g2\n"}});
        r.git({"checkout", "-q", "master"});
        QString b = r.commit("b", {{"f", "2\n"}});
        Repository repo = r.open();

        OpResult cp = Ops::cherryPick(repo, Oid::fromHex(s));
        QVERIFY2(cp.ok, qPrintable(cp.message));
        QCOMPARE(r.rev("HEAD"), cp.newCommit.hex());
        QCOMPARE(r.git({"log", "-1", "--format=%s%n%an"}).trimmed(), QString("side change\nTester"));
        QCOMPARE(r.git({"show", "HEAD:g"}), QString("g2\n"));
        QCOMPARE(r.git({"status", "--porcelain"}).trimmed(), QString());

        OpResult rv = Ops::revert(repo, Oid::fromHex(b));
        QVERIFY2(rv.ok, qPrintable(rv.message));
        QCOMPARE(r.git({"show", "HEAD:f"}), QString("1\n"));
        QVERIFY(r.git({"log", "-1", "--format=%B"}).startsWith("Revert \"b\""));

        QVERIFY(Ops::reset(repo, Oid::fromHex(a), Ops::Hard).ok);
        QCOMPARE(r.rev("HEAD"), a);
        QCOMPARE(r.git({"status", "--porcelain"}).trimmed(), QString());
    }

    void cherryPickConflict() {
        test::TestRepo r;
        r.commit("a", {{"f", "1\n"}});
        r.git({"checkout", "-q", "-b", "side"});
        QString s = r.commit("side", {{"f", "side\n"}});
        r.git({"checkout", "-q", "master"});
        r.commit("b", {{"f", "master\n"}});
        Repository repo = r.open();
        OpResult cp = Ops::cherryPick(repo, Oid::fromHex(s));
        QVERIFY(!cp.ok);
        QVERIFY(cp.conflicts);
    }

    void patches() {
        test::TestRepo r;
        QString a = r.commit("a", {{"f", "1\n"}});
        QString b = r.commit("b subject", {{"f", "2\n"}});
        Repository repo = r.open();
        QString err;
        QString email = Ops::makePatch(repo, Oid::fromHex(a), Oid::fromHex(b), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(email.contains("Subject: [PATCH] b subject"));
        QVERIFY(email.contains("-1\n+2"));
        QString diff = Ops::makePatch(repo, Oid::fromHex(b), Oid::fromHex(a), &err);
        QVERIFY(diff.startsWith("diff --git"));
        QVERIFY(diff.contains("-2\n+1"));
    }
};

QTEST_GUILESS_MAIN(TestOps)
#include "tst_ops.moc"
