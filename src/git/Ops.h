// Repository-modifying operations offered by gitk's context menus.
#pragma once

#include "git/Oid.h"

#include <QString>

namespace gitq {

class Repository;

struct OpResult {
    bool ok = true;
    bool conflicts = false;   // operation stopped with conflicts left in the work tree
    QString message;          // error or informational text
    Oid newCommit;            // commit created by the operation, if any

    static OpResult fail(const QString &m) { return {false, false, m, {}}; }
};

namespace Ops {

enum ResetType { Soft, Mixed, Hard };

OpResult checkoutBranch(const Repository &repo, const QString &branch);
OpResult checkoutCommit(const Repository &repo, const Oid &id);   // detached HEAD
OpResult createBranch(const Repository &repo, const QString &name, const Oid &target, bool force);
OpResult renameBranch(const Repository &repo, const QString &oldName, const QString &newName, bool force);
OpResult deleteBranch(const Repository &repo, const QString &name);
OpResult createTag(const Repository &repo, const QString &name, const Oid &target, const QString &message);
OpResult deleteTag(const Repository &repo, const QString &name);
// mainline: parent number (1-based) to diff against when the commit is a merge.
OpResult cherryPick(const Repository &repo, const Oid &id, int mainline = 0);
OpResult revert(const Repository &repo, const Oid &id, int mainline = 0);
OpResult reset(const Repository &repo, const Oid &id, ResetType type);

// Patch text from `from` to `to`; email format (like format-patch) when `from` is the parent of `to`.
QString makePatch(const Repository &repo, const Oid &from, const Oid &to, QString *error = nullptr);
// Commit in email format (gitk's "Write commit to file").
QString commitAsEmail(const Repository &repo, const Oid &id, QString *error = nullptr);

bool isValidBranchName(const QString &name);
bool isValidTagName(const QString &name);

} // namespace Ops
} // namespace gitq
