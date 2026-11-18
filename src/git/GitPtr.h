// RAII helpers around libgit2 handles.
#pragma once

#include <git2.h>
#if __has_include(<git2/sys/errors.h>)
#include <git2/sys/errors.h>   // git_error_clear moved here in libgit2 1.8
#endif

#include <QString>
#include <memory>
#include <stdexcept>

namespace gitq {

template <typename T, void (*Free)(T *)>
struct GitDeleter {
    void operator()(T *p) const { if (p) Free(p); }
};

template <typename T, void (*Free)(T *)>
using GitHandle = std::unique_ptr<T, GitDeleter<T, Free>>;

using RepoPtr = GitHandle<git_repository, git_repository_free>;
using CommitPtr = GitHandle<git_commit, git_commit_free>;
using TreePtr = GitHandle<git_tree, git_tree_free>;
using TreeEntryPtr = GitHandle<git_tree_entry, git_tree_entry_free>;
using BlobPtr = GitHandle<git_blob, git_blob_free>;
using ObjectPtr = GitHandle<git_object, git_object_free>;
using RefPtr = GitHandle<git_reference, git_reference_free>;
using RevwalkPtr = GitHandle<git_revwalk, git_revwalk_free>;
using DiffPtr = GitHandle<git_diff, git_diff_free>;
using PatchPtr = GitHandle<git_patch, git_patch_free>;
using IndexPtr = GitHandle<git_index, git_index_free>;
using TagPtr = GitHandle<git_tag, git_tag_free>;
using SignaturePtr = GitHandle<git_signature, git_signature_free>;
using BlamePtr = GitHandle<git_blame, git_blame_free>;
using ConfigPtr = GitHandle<git_config, git_config_free>;
using PathspecPtr = GitHandle<git_pathspec, git_pathspec_free>;
using RefIterPtr = GitHandle<git_reference_iterator, git_reference_iterator_free>;
using StatusListPtr = GitHandle<git_status_list, git_status_list_free>;

struct BufGuard {
    git_buf buf = GIT_BUF_INIT;
    ~BufGuard() { git_buf_dispose(&buf); }
    QByteArray bytes() const { return QByteArray(buf.ptr, int(buf.size)); }
};

class GitError : public std::runtime_error {
public:
    explicit GitError(const QString &msg) : std::runtime_error(msg.toStdString()) {}
    QString message() const { return QString::fromStdString(what()); }
};

// Message of the last libgit2 error, with a context prefix.
inline QString lastGitError(const QString &context = {}) {
    const git_error *e = git_error_last();
    QString msg = e && e->message ? QString::fromUtf8(e->message) : QStringLiteral("unknown error");
    return context.isEmpty() ? msg : context + QStringLiteral(": ") + msg;
}

inline void check(int rc, const QString &context = {}) {
    if (rc < 0) throw GitError(lastGitError(context));
}

// Initialises libgit2 for the lifetime of the object.
struct LibGit2Init {
    LibGit2Init() {
        git_libgit2_init();
        // Keep libgit2's object cache modest: commits are copied into our own structures anyway.
        git_libgit2_opts(GIT_OPT_SET_CACHE_MAX_SIZE, ssize_t(64) * 1024 * 1024);
    }
    ~LibGit2Init() { git_libgit2_shutdown(); }
    LibGit2Init(const LibGit2Init &) = delete;
    LibGit2Init &operator=(const LibGit2Init &) = delete;
};

} // namespace gitq
