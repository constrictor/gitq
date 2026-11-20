// Producing textual diffs (git-compatible format) with libgit2.
#pragma once

#include "git/Oid.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace gitq {

class Repository;

struct DiffOptions {
    // How a change of the commit recorded for a submodule is shown (like git's diff.submodule).
    enum Submodules {
        SubmoduleSummary,   // git's "Subproject commit" lines only
        SubmoduleLog,       // the commits between the two, and the file changes of `expandSubmodules`
        SubmoduleDiff,      // the commits, and the file changes inside every submodule
    };

    int context = 3;
    bool ignoreSpaceChange = false;   // git diff -b
    bool detectRenames = true;
    QStringList paths;                // limit to these repo-relative paths (empty: all)
    int submodules = SubmoduleSummary;
    QStringList expandSubmodules;     // SubmoduleLog: submodule paths whose file changes are included
    bool includeUntracked = false;    // index -> work tree: list untracked files (status '?')
    bool operator==(const DiffOptions &) const = default;
};

using CommitLink = QPair<Oid, QString>;   // commit id, headline

// Where a path inside a submodule lives.
struct SubmoduleLocation {
    QString prefix;     // path of the (innermost) submodule containing it
    QString repoPath;   // git directory of the submodule's repository
    Oid commit;         // commit recorded for the submodule (work tree pseudo id: its work tree)
    QString path;       // the path inside the submodule (empty: the submodule itself)
};

// A change of the commit recorded for a submodule.
struct SubmoduleChange {
    QString path;               // of the submodule in the diffed repository (nested ones: full path)
    QString repoPath;           // git directory of the submodule's repository; empty if unavailable
    QChar status;               // A, M, D
    Oid oldId, newId;           // recorded commits (null when added / deleted)
    bool newIsWorkdir = false;  // new side is the submodule's work tree (local changes)
    bool oldIsWorkdir = false;
    bool detailed = false;      // commits looked up (not SubmoduleSummary)
    QList<CommitLink> added;    // in new but not in old, newest first (git's '>')
    QList<CommitLink> removed;  // in old but not in new: the new commit is a rewind (git's '<')
    int moreAdded = 0, moreRemoved = 0;   // commits left out of the lists
    bool expandable = false;    // its file changes can be shown (SubmoduleLog: on request)
    bool expanded = false;      // the submodule's file changes follow it in DiffResult::files
    QStringList notes;          // why commits or files can't be shown, local modifications, ...
};

struct FileDiff {
    QString oldPath;
    QString newPath;
    QChar status = QLatin1Char('M');  // A, M, D, R, C, T; '?' untracked, 'U' unmerged (local changes)
    int numParents = 1;               // >1 for combined (merge) diffs
    bool binary = false;
    QStringList lines;                // full text, git format, without trailing newlines
    int submodule = -1;               // index into DiffResult::submodules if this is a submodule
    QString inSubmodule;              // innermost submodule containing this file (empty: none)

    QString path() const { return newPath.isEmpty() ? oldPath : newPath; }
    // Numbers of added and removed lines (for combined diffs: lines added / removed against any parent).
    void countChanges(int *added, int *removed) const;

    // The hunks in `lines`: index of each "@@" line and of the line after the hunk's last line.
    struct Hunk {
        int start, end;
    };
    QList<Hunk> hunks() const;
};

struct DiffResult {
    QList<FileDiff> files;            // a submodule's file changes follow its entry
    QList<SubmoduleChange> submodules;
    QString error;

    // The expanded submodule whose files contain `path`, or the submodule at `path` itself.
    const SubmoduleChange *submoduleFor(const QString &path) const;
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

    // All file paths in the tree of `id` (Tree mode of the file list). With `submodules`, the files
    // of available submodules are listed in place of the submodule.
    static QStringList treeFiles(const Repository &repo, const Oid &id, bool submodules = false);
    // Contents of a file at a commit (pseudo ids read from index / work tree). A path inside a
    // submodule is read from the submodule's repository at the commit recorded for it.
    static QByteArray fileContents(const Repository &repo, const Oid &id, const QString &path, bool *found = nullptr);

    // Whether `path` at commit `id` (or the index / work tree) is a submodule or inside one whose
    // repository is available.
    static bool locateInSubmodule(const Repository &repo, const Oid &id, const QString &path, SubmoduleLocation *out);

    // Word-level diff of removed vs added text.
    static QList<WordSeg> wordDiff(const QString &oldText, const QString &newText);
};

} // namespace gitq
