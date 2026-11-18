#include "TestRepo.h"
#include "git/LogLoader.h"

#include <QHash>

using namespace gitq;

namespace {

struct Log {
    QStringList order;
    QHash<QString, QStringList> parents;
    QSet<QString> boundary;
};

Log load(const Repository &repo, const QStringList &args) {
    QString err;
    LogOptions o = RevArgs::parse(args, &repo, &err);
    Log log;
    QString e = loadLog(repo, o, [&](CommitPtrList &&batch) {
        for (const auto &c : batch) {
            QString id = c->id.hex();
            log.order << id;
            QStringList ps;
            for (const Oid &p : c->parents) ps << p.hex();
            log.parents.insert(id, ps);
            if (c->boundary) log.boundary.insert(id);
        }
    });
    if (!e.isEmpty()) qWarning() << e;
    return log;
}

// Expected output from `git rev-list --parents [--boundary] args`.
Log expected(test::TestRepo &r, QStringList args, bool boundary = false) {
    QStringList cmd{"rev-list", "--parents", "--topo-order"};
    if (boundary) cmd << "--boundary";
    cmd << args;
    Log log;
    for (const QString &line : r.git(cmd).split('\n', Qt::SkipEmptyParts)) {
        QStringList parts = line.split(' ');
        QString id = parts.takeFirst();
        if (id.startsWith('-')) {
            id = id.mid(1);
            log.boundary.insert(id);
            parts.clear();
        }
        log.order << id;
        log.parents.insert(id, parts);
    }
    return log;
}

void verifyTopo(const Log &log) {
    QHash<QString, int> pos;
    for (int i = 0; i < log.order.size(); ++i) pos.insert(log.order[i], i);
    for (auto it = log.parents.cbegin(); it != log.parents.cend(); ++it)
        for (const QString &p : it.value())
            if (pos.contains(p)) QVERIFY2(pos[p] > pos[it.key()], "parent shown before child");
}

void compare(const Log &got, const Log &want) {
    QCOMPARE(QSet<QString>(got.order.cbegin(), got.order.cend()), QSet<QString>(want.order.cbegin(), want.order.cend()));
    QCOMPARE(got.order.size(), want.order.size());
    for (const QString &id : want.order)
        if (!want.boundary.contains(id)) QCOMPARE(got.parents.value(id), want.parents.value(id));
    QCOMPARE(got.boundary, want.boundary);
    verifyTopo(got);
}

} // namespace

class TestLogLoader : public QObject {
    Q_OBJECT
    LibGit2Init init;

    // master: a - b - c - M(merge topic) - e
    // topic:   \- t1 - t2 /
    void buildHistory(test::TestRepo &r) {
        r.commit("a", {{"src/main.c", "int main;\n"}, {"README", "readme\n"}});
        QString b = r.commit("b", {{"README", "readme 2\n"}});
        r.git({"checkout", "-q", "-b", "topic"});
        r.commit("t1", {{"src/main.c", "int main(void);\n"}});
        r.commit("t2", {{"doc/guide", "guide\n"}});
        r.git({"checkout", "-q", "master"});
        r.commit("c", {{"README", "readme 3\n"}});
        r.m_tick++;
        r.git({"merge", "-q", "--no-ff", "-m", "Merge topic", "topic"});
        r.commit("e", {{"src/util.c", "util\n"}});
        r.git({"tag", "v1", b});
    }

private slots:
    void fullHistory() {
        test::TestRepo r;
        buildHistory(r);
        Repository repo = r.open();
        compare(load(repo, {}), expected(r, {"HEAD"}));
        compare(load(repo, {"--all"}), expected(r, {"--all"}));
    }

    void rangesWithBoundary() {
        test::TestRepo r;
        buildHistory(r);
        Repository repo = r.open();
        compare(load(repo, {"v1..master"}), expected(r, {"v1..master"}, true));
        compare(load(repo, {"master~1...topic"}), expected(r, {"master~1...topic"}, true));
    }

    void pathLimited() {
        test::TestRepo r;
        buildHistory(r);
        Repository repo = r.open();
        compare(load(repo, {"--", "src"}), expected(r, {"HEAD", "--", "src"}));
        compare(load(repo, {"--", "README"}), expected(r, {"HEAD", "--", "README"}));
        compare(load(repo, {"--", "doc"}), expected(r, {"HEAD", "--", "doc"}));
        compare(load(repo, {"--", "src/main.c"}), expected(r, {"HEAD", "--", "src/main.c"}));
    }

    void filters() {
        test::TestRepo r;
        buildHistory(r);
        Repository repo = r.open();
        Log got = load(repo, {"--no-merges"});
        Log want = expected(r, {"--no-merges", "HEAD"});
        QCOMPARE(QSet<QString>(got.order.cbegin(), got.order.cend()), QSet<QString>(want.order.cbegin(), want.order.cend()));
        got = load(repo, {"--grep=^t"});
        QCOMPARE(got.order.size(), 2);
        got = load(repo, {"-n", "3"});
        QCOMPARE(got.order.size(), 3);
        // git prints all parents with --first-parent; gitq keeps only the followed one.
        got = load(repo, {"--first-parent"});
        want = expected(r, {"--first-parent", "HEAD"});
        QCOMPARE(got.order, want.order);
        for (const QString &id : want.order)
            QCOMPARE(got.parents.value(id), want.parents.value(id).mid(0, 1));
    }

    void localChanges() {
        test::TestRepo r;
        buildHistory(r);
        r.write("README", "changed\n");
        r.write("src/util.c", "staged\n");
        r.git({"add", "src/util.c"});
        Repository repo = r.open();
        QString err;
        LogOptions o = RevArgs::parse({}, &repo, &err);
        o.showLocalChanges = true;
        QList<Oid> ids;
        QList<QList<Oid>> parents;
        loadLog(repo, o, [&](CommitPtrList &&b) {
            for (const auto &c : b) {
                ids << c->id;
                parents << c->parents;
            }
        });
        QVERIFY(ids.size() >= 2);
        QVERIFY(ids[0].isWorkdir());
        QVERIFY(ids[1].isIndex());
        QCOMPARE(parents[0], QList<Oid>{Oid::indexId()});
        QCOMPARE(parents[1].first().hex(), r.rev("HEAD"));
    }
};

QTEST_GUILESS_MAIN(TestLogLoader)
#include "tst_logloader.moc"
