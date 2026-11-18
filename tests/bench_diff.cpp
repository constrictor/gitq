// Manual benchmark: bench_diff <repo> <rev> — times diff computation and rendering.
#include "git/DiffEngine.h"
#include "git/Repository.h"
#include "Settings.h"
#include "ui/DiffView.h"

#include <QApplication>
#include <QElapsedTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    gitq::LibGit2Init init;
    gitq::Settings::instance().load();
    gitq::Repository repo = gitq::Repository::open(app.arguments().value(1));
    gitq::Oid id = repo.resolveCommit(app.arguments().value(2));
    QElapsedTimer t;
    t.start();
    gitq::DiffResult d = gitq::DiffEngine::diffCommit(repo, id, gitq::DiffOptions());
    qint64 tDiff = t.restart();
    int lines = 0;
    for (const auto &f : d.files) lines += int(f.lines.size());
    gitq::DiffView view;
    view.resize(800, 600);
    view.show();
    view.appendDiff(d);
    qint64 tRender = t.restart();
    view.setWordMode(gitq::DiffView::ColorWords);
    view.clearAll();
    view.appendDiff(d);
    qint64 tWords = t.elapsed();
    printf("%lld files, %d lines: diff %lld ms, render %lld ms, render (color words) %lld ms\n",
           qint64(d.files.size()), lines, tDiff, tRender, tWords);
}
