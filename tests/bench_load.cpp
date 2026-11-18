// Manual benchmark: bench_load <repo> [rev-list args...] — prints timing and memory.
#include "git/LogLoader.h"
#include "git/Repository.h"
#include "model/CommitModel.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <malloc.h>

static long rssKb() {
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly)) return 0;
    for (const QByteArray &l : f.readAll().split('\n'))
        if (l.startsWith("RssAnon:")) return l.mid(8).trimmed().split(' ').first().toLong();
    return 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    gitq::LibGit2Init init;
    QStringList args = app.arguments().mid(2);
    gitq::Repository repo = gitq::Repository::open(app.arguments().value(1));
    QString err;
    gitq::LogOptions o = gitq::RevArgs::parse(args, &repo, &err);
    long base = rssKb();
    QElapsedTimer t;
    t.start();
    gitq::CommitPtrList all;
    {
        gitq::Repository r2 = gitq::Repository::open(app.arguments().value(1));
        loadLog(r2, o, [&](gitq::CommitPtrList &&b) { all.append(b); });
    }
    malloc_trim(0);
    qint64 tLoad = t.elapsed();
    long afterLoad = rssKb();
    qint64 msgBytes = 0;
    for (const auto &c : all) msgBytes += c->rawMessage.size();
    gitq::CommitModel model;
    model.appendCommits(all);
    model.finishLoading();
    qint64 tModel = t.elapsed() - tLoad;
    long afterModel = rssKb();
    int segs = 0;
    for (int i = 0; i < model.graph().rowCount(); ++i)
        segs += int(model.graph().row(i).top.size() + model.graph().row(i).bottom.size());
    struct mallinfo2 mi = mallinfo2();
    printf("malloc in use: %zu KB, free in arenas: %zu KB\n", mi.uordblks / 1024, mi.fordblks / 1024);
    printf("%lld commits; load %lld ms, model %lld ms\n", qint64(all.size()), tLoad, tModel);
    printf("rss: base %ld KB, +load %ld KB, +model %ld KB; messages %lld KB; graph max width %d, %d segs\n", base,
           afterLoad - base, afterModel - afterLoad, msgBytes / 1024, model.graph().maxWidth(), segs);
}
