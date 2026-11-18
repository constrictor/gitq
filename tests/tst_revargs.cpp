#include "TestRepo.h"
#include "git/RevArgs.h"

using namespace gitq;

class TestRevArgs : public QObject {
    Q_OBJECT
    LibGit2Init init;
private slots:
    void parseOptions() {
        QString err;
        LogOptions o = RevArgs::parse({"--all", "--date-order", "-n", "5", "--author=bob", "--since=2024-01-01",
                                       "--first-parent", "--no-merges", "--", "src", "doc"},
                                      nullptr, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(o.all);
        QCOMPARE(o.order, LogOptions::DateOrder);
        QCOMPARE(o.maxCount, 5);
        QCOMPARE(o.authors, QStringList{"bob"});
        QVERIFY(o.since > 0);
        QVERIFY(o.firstParent);
        QCOMPARE(o.maxParents, 1);
        QCOMPARE(o.paths, (QStringList{"src", "doc"}));
    }

    void roundTrip() {
        QString err;
        QStringList args{"--branches", "--grep=fix", "-i", "master", "^v1", "--", "a"};
        LogOptions o = RevArgs::parse(args, nullptr, &err);
        LogOptions o2 = RevArgs::parse(RevArgs::toArgs(o), nullptr, &err);
        QVERIFY(o == o2);
    }

    void dates() {
        const qint64 now = 1700000000;
        QCOMPARE(RevArgs::parseDate("@1600000000", now), qint64(1600000000));
        QCOMPARE(RevArgs::parseDate("2 days ago", now), now - 2 * 86400);
        QCOMPARE(RevArgs::parseDate("1 week ago", now), now - 7 * 86400);
        QCOMPARE(RevArgs::parseDate("3.days.ago", now), now - 3 * 86400);
        QVERIFY(RevArgs::parseDate("2023-11-14", now) > 0);
        QCOMPARE(RevArgs::parseDate("March 17, 2009 15:27:38", now), RevArgs::parseDate("2009-03-17 15:27:38", now));
        QCOMPARE(RevArgs::parseDate("garbage", now), qint64(0));
    }

    void resolveRanges() {
        test::TestRepo r;
        QString a = r.commit("a");
        QString b = r.commit("b");
        r.git({"checkout", "-q", "-b", "topic", a});
        QString c = r.commit("c");
        Repository repo = r.open();
        QString err;

        ResolvedRevs rr = RevArgs::resolve(repo, RevArgs::parse({"master..topic"}, &repo, &err), &err);
        QCOMPARE(rr.include, QList<Oid>{Oid::fromHex(c)});
        QCOMPARE(rr.exclude, QList<Oid>{Oid::fromHex(b)});

        rr = RevArgs::resolve(repo, RevArgs::parse({"master...topic"}, &repo, &err), &err);
        QCOMPARE(rr.include.size(), 2);
        QCOMPARE(rr.exclude, QList<Oid>{Oid::fromHex(a)});

        rr = RevArgs::resolve(repo, RevArgs::parse({"topic^!"}, &repo, &err), &err);
        QCOMPARE(rr.include, QList<Oid>{Oid::fromHex(c)});
        QCOMPARE(rr.exclude, QList<Oid>{Oid::fromHex(a)});

        rr = RevArgs::resolve(repo, RevArgs::parse({}, &repo, &err), &err);
        QCOMPARE(rr.include, QList<Oid>{Oid::fromHex(c)});   // HEAD

        rr = RevArgs::resolve(repo, RevArgs::parse({"--branches"}, &repo, &err), &err);
        QCOMPARE(rr.include.size(), 2);
    }

    void pathDetection() {
        test::TestRepo r;
        r.commit("a", {{"file.txt", "x\n"}});
        Repository repo = r.open();
        QString err;
        QString old = QDir::currentPath();
        QDir::setCurrent(r.path());
        LogOptions o = RevArgs::parse({"master", "file.txt"}, &repo, &err);
        QDir::setCurrent(old);
        QCOMPARE(o.revisions, QStringList{"master"});
        QCOMPARE(o.paths, QStringList{"file.txt"});
    }
};

QTEST_GUILESS_MAIN(TestRevArgs)
#include "tst_revargs.moc"
