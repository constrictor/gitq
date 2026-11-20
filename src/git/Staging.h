// Preparing a commit: staging, unstaging and discarding files and hunks, and committing.
// Changes go through the git CLI (see GitCli.h); results are reported as OpResult like Ops.h.
#pragma once

#include "git/Ops.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace gitq {

class Repository;

// A copy of a file's work tree content saved before it was discarded or deleted, so that it can be
// restored. The content is written to the object database as a blob (unfiltered bytes) and each
// backup is also appended to <git dir>/gitq/discarded.
struct DiscardBackup {
    QString path;              // repository-relative
    QString blob;              // id of the saved content
    bool executable = false;
    bool symlink = false;      // the blob holds the link target
    bool untracked = false;    // the file was new (deleted rather than reverted)
    QDateTime time;
};

// A hunk as shown in a diff: its "@@ -a,b +c,d @@" line and the lines after it.
struct HunkRef {
    QString path;
    QString header;
    QStringList body;
    int context = 3;           // lines of context the diff was made with
};

// The parts of a patch for one file: everything before the first hunk, and each hunk.
struct SplitPatch {
    QByteArray header;
    QList<QByteArray> hunks;
};

struct CommitRequest {
    QString message;
    bool amend = false;
    bool signOff = false;
};

struct StagedSummary {
    int files = 0;
    int added = 0, removed = 0;
};

namespace Ops {

enum HunkAction { StageHunk, UnstageHunk, DiscardHunk };

// File level. Paths are repository-relative; a renamed file is given by both its paths.
OpResult stageFiles(const Repository &repo, const QStringList &paths);        // git add -A
OpResult unstageFiles(const Repository &repo, const QStringList &paths);      // git restore --staged
// Reverts the work tree to the index (git restore --worktree), after backing the files up.
OpResult discardFiles(const Repository &repo, const QStringList &paths, QList<DiscardBackup> *backups = nullptr);
// Deletes untracked files, after backing them up; directories left empty are removed too.
OpResult deleteUntracked(const Repository &repo, const QStringList &paths, QList<DiscardBackup> *backups = nullptr);
// Writes a backed-up file back to the work tree.
OpResult restoreDiscarded(const Repository &repo, const DiscardBackup &backup);
// Whether the work tree file still has the content it had when it was backed up (or is still gone).
bool backupIsCurrent(const Repository &repo, const DiscardBackup &backup);

// Hunk level. The hunk is looked up again in a fresh diff of the file (index vs work tree, or HEAD
// vs index for UnstageHunk); if it is no longer there the file changed and nothing is done.
OpResult applyHunk(const Repository &repo, const HunkRef &hunk, HunkAction action,
                   QList<DiscardBackup> *backups = nullptr);
SplitPatch splitPatch(const QByteArray &patch);
// The "@@ -a,b +c,d @@" part of a hunk header line (without the function context).
QString hunkRange(const QString &headerLine);

// Commit. commitArgs() / commitResult() are the two halves used by an asynchronous commit.
QStringList commitArgs(const CommitRequest &req);
OpResult commitResult(const Repository &repo, bool ok, const QString &output);
OpResult commit(const Repository &repo, const CommitRequest &req);   // synchronous

// Message of HEAD (for amending), and the prepared message of a merge in progress (empty if none).
QString headMessage(const Repository &repo);
QString mergeMessage(const Repository &repo);
StagedSummary stagedSummary(const Repository &repo);

// Draft of the next commit message, kept in <git dir>/gitq/COMMIT_DRAFT.
QString loadDraft(const Repository &repo);
void saveDraft(const Repository &repo, const QString &text);

} // namespace Ops
} // namespace gitq
