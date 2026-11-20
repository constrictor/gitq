#pragma once

#include "git/CommitInfo.h"
#include "git/GitPtr.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace gitq {

struct RefSet {
    QList<RefInfo> refs;
    QMultiHash<Oid, int> byCommit;   // commit id -> index into refs
    QString headBranch;               // short name of checked-out branch; empty if detached
    Oid head;                         // commit HEAD points to (null if unborn)

    QList<RefInfo> refsFor(const Oid &id) const;
};

// Thin wrapper around a git_repository. Not thread-safe: open one per thread.
class Repository {
public:
    // Discovers the repository containing `path`. Throws GitError on failure.
    static Repository open(const QString &path);

    Repository() = default;
    Repository(Repository &&) = default;
    Repository &operator=(Repository &&) = default;

    bool isValid() const { return bool(m_repo); }
    git_repository *raw() const { return m_repo.get(); }
    QString path() const { return m_path; }        // .git directory
    QString workdir() const;                       // empty for bare repos
    bool isBare() const;

    // Opens the repository of the submodule at `path` (relative to the work tree): the checked-out
    // submodule, or else its repository under .git/modules. Returns an invalid Repository if neither
    // exists.
    Repository openSubmodule(const QString &path) const;
    // Submodule status flags (GIT_SUBMODULE_STATUS_*) of the submodule at `path`; 0 if unknown.
    unsigned submoduleStatus(const QString &path) const;

    RefSet loadRefs() const;
    // Resolve a revision expression to a commit id. Returns null Oid if not found.
    Oid resolveCommit(const QString &spec, QString *error = nullptr) const;
    CommitPtr lookupCommit(const Oid &id) const;
    CommitInfo readCommit(const Oid &id) const;
    // `pool`, when given, is used to share identical name/email strings between commits.
    static void fillCommitInfo(git_commit *c, CommitInfo &info, QSet<QString> *pool = nullptr);

    // Merge base of two commits (null if none).
    Oid mergeBase(const Oid &a, const Oid &b) const;
    // Whether `ancestor` is reachable from `descendant` (or equal).
    bool isAncestor(const Oid &ancestor, const Oid &descendant) const;

    // Whether there are local modifications in the index / working tree (with `untracked`, new
    // files that are not ignored count as work tree changes).
    void localChanges(bool *indexChanged, bool *workdirChanged, bool untracked = false) const;

    QString configString(const QString &key, const QString &def = {}) const;

private:
    static Repository wrap(git_repository *r);
    // Fills in the upstream of a local branch and how far apart they are.
    void upstreamOf(git_reference *branch, RefInfo &info) const;

    RepoPtr m_repo;
    QString m_path;
};

Person personFrom(const git_signature *sig, QSet<QString> *pool = nullptr);

} // namespace gitq
