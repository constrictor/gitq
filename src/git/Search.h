// Commit searches behind gitk's "Find" bar.
#pragma once

#include "git/CommitInfo.h"

#include <QObject>
#include <QRegularExpression>
#include <QThread>
#include <atomic>
#include <memory>

namespace gitq {

class Repository;

struct FindSpec {
    enum Type { Containing, TouchingPaths, AddingRemoving, ChangingLines };
    enum Mode { Exact, IgnoreCase, Regexp };
    enum Field { AllFields, Headline, Comments, Author, Committer };

    Type type = Containing;
    Mode mode = Exact;
    Field field = AllFields;
    QString text;

    bool isEmpty() const { return text.isEmpty(); }
    bool operator==(const FindSpec &) const = default;
};

namespace Search {

// In-memory match for FindSpec::Containing.
bool matchesFields(const CommitInfo &c, const FindSpec &spec);
// Positions [start, length) of matches of `spec` in `text` (for highlighting).
QList<QPair<int, int>> matchRanges(const QString &text, const FindSpec &spec);
QRegularExpression regexFor(const FindSpec &spec);
// Slow, per-commit content searches (touching paths, -S, -G). Thread-agnostic.
bool matchesContent(const Repository &repo, const Oid &id, const FindSpec &spec);

} // namespace Search

// Runs content searches over a list of commits in a background thread.
class SearchWorker : public QObject {
    Q_OBJECT
public:
    explicit SearchWorker(QObject *parent = nullptr);
    ~SearchWorker() override;
    void start(const QString &repoPath, const QList<Oid> &commits, const FindSpec &spec);
    void cancel();

signals:
    void matched(const QList<gitq::Oid> &ids);
    void progress(int done, int total);
    void finished();

private:
    QThread *m_thread = nullptr;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    int m_generation = 0;
};

} // namespace gitq
