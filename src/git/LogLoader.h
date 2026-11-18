// Background loading of the commit list (the equivalent of gitk's `git log --parents --boundary`).
#pragma once

#include "git/CommitInfo.h"
#include "git/RevArgs.h"

#include <QObject>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>

namespace gitq {

class Repository;

// Synchronous core, usable from tests. Calls `sink` with batches of commits in display order.
// Returns an error message (empty on success). Stops early when `cancel` becomes true.
QString loadLog(const Repository &repo, const LogOptions &opts,
                const std::function<void(CommitPtrList &&)> &sink,
                const std::atomic<bool> *cancel = nullptr, int batchSize = 2000);

// True when commit `c` touches the given repo-relative paths compared to `parent`
// (`parent` may be null for root commits).
class PathFilter {
public:
    PathFilter(git_repository *repo, const QStringList &paths);
    ~PathFilter();
    bool isEmpty() const { return m_paths.isEmpty(); }
    // Whether trees are identical with respect to the paths. Either tree may be null.
    bool treeSame(git_tree *a, git_tree *b) const;
    bool matchesPath(const QString &path) const;

private:
    git_repository *m_repo;
    QStringList m_paths;
    bool m_literal = true;
    git_pathspec *m_spec = nullptr;
};

class LogLoader : public QObject {
    Q_OBJECT
public:
    explicit LogLoader(QObject *parent = nullptr);
    ~LogLoader() override;

    // Starts loading in a background thread; cancels any load in progress.
    void start(const QString &repoPath, const LogOptions &opts);
    void cancel();
    bool isRunning() const;

signals:
    void commitsLoaded(const gitq::CommitPtrList &batch);
    void finished(const QString &error);

private:
    QThread *m_thread = nullptr;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    int m_generation = 0;
};

} // namespace gitq

Q_DECLARE_METATYPE(gitq::CommitPtrList)
