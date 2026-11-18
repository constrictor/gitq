// Producing textual diffs (git-compatible format) with libgit2.
#pragma once

#include "git/Oid.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace gitq {

class Repository;

struct DiffOptions {
    int context = 3;
    bool ignoreSpaceChange = false;   // git diff -b
    bool detectRenames = true;
    QStringList paths;                // limit to these repo-relative paths (empty: all)
    bool operator==(const DiffOptions &) const = default;
};

struct FileDiff {
    QString oldPath;
    QString newPath;
    QChar status = QLatin1Char('M');  // A, M, D, R, C, T
    int numParents = 1;               // >1 for combined (merge) diffs
    bool binary = false;
    QStringList lines;                // full text, git format, without trailing newlines

    QString path() const { return newPath.isEmpty() ? oldPath : newPath; }
};

struct DiffResult {
    QList<FileDiff> files;
    QString error;
};

// One run of a line after word diffing.
struct WordSeg {
    enum Kind { Same, Removed, Added };
    Kind kind;
    QString text;
};

class DiffEngine {
public:
    // Diff of a commit against its parents: normal diff for 0/1 parent, combined (--cc) for merges.
    // Pseudo ids: Oid::workdirId() = index vs work tree; Oid::indexId() = HEAD vs index.
    static DiffResult diffCommit(const Repository &repo, const Oid &id, const DiffOptions &opts);
    // Diff from `from` to `to` (either may be a pseudo id).
    static DiffResult diffBetween(const Repository &repo, const Oid &from, const Oid &to, const DiffOptions &opts);
    // Diff of a merge against one of its parents (1-based index), like `git diff M^n M`.
    static DiffResult diffAgainstParent(const Repository &repo, const Oid &id, int parent, const DiffOptions &opts);

    // Combined diff of `result` against `parents` (exposed for tests).
    static FileDiff combinedFileDiff(const QString &path, const QList<QByteArray> &parents, const QByteArray &result,
                                     const DiffOptions &opts, bool dense, const QStringList &parentIds,
                                     const QString &resultId);

    // All file paths in the tree of `id` (Tree mode of the file list).
    static QStringList treeFiles(const Repository &repo, const Oid &id);
    // Contents of a file at a commit (pseudo ids read from index / work tree).
    static QByteArray fileContents(const Repository &repo, const Oid &id, const QString &path, bool *found = nullptr);

    // Word-level diff of removed vs added text.
    static QList<WordSeg> wordDiff(const QString &oldText, const QString &newText);
};

} // namespace gitq
