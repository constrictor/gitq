#include "git/Ops.h"

#include "git/DiffEngine.h"
#include "git/GitPtr.h"
#include "git/Repository.h"

namespace gitq::Ops {

namespace {

OpResult lastError(const QString &ctx) {
    OpResult r = OpResult::fail(lastGitError(ctx));
    git_error_clear();
    return r;
}

ObjectPtr lookupObject(const Repository &repo, const Oid &id) {
    git_object *o = nullptr;
    if (git_object_lookup(&o, repo.raw(), id.ptr(), GIT_OBJECT_COMMIT) < 0) {
        git_error_clear();
        return {};
    }
    return ObjectPtr(o);
}

git_checkout_options safeCheckout() {
    git_checkout_options o = GIT_CHECKOUT_OPTIONS_INIT;
    o.checkout_strategy = GIT_CHECKOUT_SAFE;
    return o;
}

// Commits the current index on top of HEAD after a clean cherry-pick/revert.
OpResult commitIndex(const Repository &repo, const git_signature *author, const QString &message) {
    git_index *idx = nullptr;
    if (git_repository_index(&idx, repo.raw()) < 0) return lastError(QStringLiteral("index"));
    IndexPtr index(idx);
    if (git_index_has_conflicts(idx)) {
        OpResult r;
        r.ok = false;
        r.conflicts = true;
        r.message = QStringLiteral("The operation resulted in conflicts. Resolve them in the work tree and commit.");
        return r;
    }
    git_oid treeId;
    if (git_index_write_tree(&treeId, idx) < 0) return lastError(QStringLiteral("write tree"));
    git_tree *t = nullptr;
    if (git_tree_lookup(&t, repo.raw(), &treeId) < 0) return lastError(QStringLiteral("tree"));
    TreePtr tree(t);

    Oid headId = repo.resolveCommit(QStringLiteral("HEAD"));
    CommitPtr head = repo.lookupCommit(headId);
    if (head && git_oid_equal(git_commit_tree_id(head.get()), &treeId)) {
        git_repository_state_cleanup(repo.raw());
        return OpResult::fail(QStringLiteral("No changes resulted; nothing was committed."));
    }

    git_signature *sig = nullptr;
    if (git_signature_default(&sig, repo.raw()) < 0) return lastError(QStringLiteral("committer identity"));
    SignaturePtr committer(sig);
    const git_commit *parents[1] = {head.get()};
    git_oid newId;
    QByteArray msg = message.toUtf8();
    if (git_commit_create(&newId, repo.raw(), "HEAD", author ? author : sig, sig, nullptr, msg.constData(),
                          tree.get(), head ? 1 : 0, parents) < 0)
        return lastError(QStringLiteral("commit"));
    git_repository_state_cleanup(repo.raw());
    OpResult r;
    r.newCommit = Oid(newId);
    return r;
}

} // namespace

bool isValidBranchName(const QString &name) {
    int valid = 0;
    QByteArray n = name.toUtf8();
    return !name.isEmpty() && git_branch_name_is_valid(&valid, n.constData()) == 0 && valid;
}

bool isValidTagName(const QString &name) {
    int valid = 0;
    QByteArray n = name.toUtf8();
    return !name.isEmpty() && git_tag_name_is_valid(&valid, n.constData()) == 0 && valid;
}

OpResult checkoutBranch(const Repository &repo, const QString &branch) {
    git_reference *ref = nullptr;
    QByteArray b = branch.toUtf8();
    if (git_branch_lookup(&ref, repo.raw(), b.constData(), GIT_BRANCH_LOCAL) < 0)
        return lastError(QStringLiteral("branch %1").arg(branch));
    RefPtr guard(ref);
    git_object *target = nullptr;
    if (git_reference_peel(&target, ref, GIT_OBJECT_COMMIT) < 0) return lastError(branch);
    ObjectPtr tg(target);
    git_checkout_options opts = safeCheckout();
    if (git_checkout_tree(repo.raw(), target, &opts) < 0) return lastError(QStringLiteral("checkout"));
    if (git_repository_set_head(repo.raw(), git_reference_name(ref)) < 0) return lastError(QStringLiteral("set HEAD"));
    return {};
}

OpResult checkoutCommit(const Repository &repo, const Oid &id) {
    ObjectPtr obj = lookupObject(repo, id);
    if (!obj) return OpResult::fail(QStringLiteral("commit not found"));
    git_checkout_options opts = safeCheckout();
    if (git_checkout_tree(repo.raw(), obj.get(), &opts) < 0) return lastError(QStringLiteral("checkout"));
    if (git_repository_set_head_detached(repo.raw(), id.ptr()) < 0) return lastError(QStringLiteral("set HEAD"));
    return {};
}

OpResult createBranch(const Repository &repo, const QString &name, const Oid &target, bool force) {
    if (!isValidBranchName(name)) return OpResult::fail(QStringLiteral("Invalid branch name: %1").arg(name));
    CommitPtr c = repo.lookupCommit(target);
    if (!c) return OpResult::fail(QStringLiteral("commit not found"));
    git_reference *ref = nullptr;
    QByteArray n = name.toUtf8();
    if (git_branch_create(&ref, repo.raw(), n.constData(), c.get(), force) < 0) return lastError(QStringLiteral("create branch"));
    git_reference_free(ref);
    return {};
}

OpResult renameBranch(const Repository &repo, const QString &oldName, const QString &newName, bool force) {
    if (!isValidBranchName(newName)) return OpResult::fail(QStringLiteral("Invalid branch name: %1").arg(newName));
    git_reference *ref = nullptr;
    QByteArray o = oldName.toUtf8(), n = newName.toUtf8();
    if (git_branch_lookup(&ref, repo.raw(), o.constData(), GIT_BRANCH_LOCAL) < 0) return lastError(oldName);
    RefPtr guard(ref);
    git_reference *out = nullptr;
    if (git_branch_move(&out, ref, n.constData(), force) < 0) return lastError(QStringLiteral("rename branch"));
    git_reference_free(out);
    return {};
}

OpResult deleteBranch(const Repository &repo, const QString &name) {
    git_reference *ref = nullptr;
    QByteArray n = name.toUtf8();
    if (git_branch_lookup(&ref, repo.raw(), n.constData(), GIT_BRANCH_LOCAL) < 0) return lastError(name);
    RefPtr guard(ref);
    if (git_branch_is_checked_out(ref) == 1)
        return OpResult::fail(QStringLiteral("Cannot delete the currently checked out branch"));
    if (git_branch_delete(ref) < 0) return lastError(QStringLiteral("delete branch"));
    return {};
}

OpResult createTag(const Repository &repo, const QString &name, const Oid &target, const QString &message) {
    if (!isValidTagName(name)) return OpResult::fail(QStringLiteral("Invalid tag name: %1").arg(name));
    ObjectPtr obj = lookupObject(repo, target);
    if (!obj) return OpResult::fail(QStringLiteral("commit not found"));
    git_oid out;
    QByteArray n = name.toUtf8();
    if (message.trimmed().isEmpty()) {
        if (git_tag_create_lightweight(&out, repo.raw(), n.constData(), obj.get(), 0) < 0)
            return lastError(QStringLiteral("create tag"));
    } else {
        git_signature *sig = nullptr;
        if (git_signature_default(&sig, repo.raw()) < 0) return lastError(QStringLiteral("tagger identity"));
        SignaturePtr tagger(sig);
        QByteArray m = message.toUtf8();
        if (!m.endsWith('\n')) m += '\n';
        if (git_tag_create(&out, repo.raw(), n.constData(), obj.get(), sig, m.constData(), 0) < 0)
            return lastError(QStringLiteral("create tag"));
    }
    return {};
}

OpResult deleteTag(const Repository &repo, const QString &name) {
    QByteArray n = name.toUtf8();
    if (git_tag_delete(repo.raw(), n.constData()) < 0) return lastError(QStringLiteral("delete tag"));
    return {};
}

OpResult cherryPick(const Repository &repo, const Oid &id, int mainline) {
    CommitPtr c = repo.lookupCommit(id);
    if (!c) return OpResult::fail(QStringLiteral("commit not found"));
    if (git_commit_parentcount(c.get()) > 1 && mainline <= 0) mainline = 1;
    git_cherrypick_options opts = GIT_CHERRYPICK_OPTIONS_INIT;
    opts.mainline = unsigned(mainline);
    opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    if (git_cherrypick(repo.raw(), c.get(), &opts) < 0) return lastError(QStringLiteral("cherry-pick"));
    return commitIndex(repo, git_commit_author(c.get()), QString::fromUtf8(git_commit_message(c.get())));
}

OpResult revert(const Repository &repo, const Oid &id, int mainline) {
    CommitPtr c = repo.lookupCommit(id);
    if (!c) return OpResult::fail(QStringLiteral("commit not found"));
    if (git_commit_parentcount(c.get()) > 1 && mainline <= 0) mainline = 1;
    git_revert_options opts = GIT_REVERT_OPTIONS_INIT;
    opts.mainline = unsigned(mainline);
    opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    if (git_revert(repo.raw(), c.get(), &opts) < 0) return lastError(QStringLiteral("revert"));
    QString subject = QString::fromUtf8(git_commit_summary(c.get()));
    QString msg = QStringLiteral("Revert \"%1\"\n\nThis reverts commit %2.\n").arg(subject, id.hex());
    return commitIndex(repo, nullptr, msg);
}

OpResult reset(const Repository &repo, const Oid &id, ResetType type) {
    ObjectPtr obj = lookupObject(repo, id);
    if (!obj) return OpResult::fail(QStringLiteral("commit not found"));
    git_reset_t t = type == Soft ? GIT_RESET_SOFT : type == Mixed ? GIT_RESET_MIXED : GIT_RESET_HARD;
    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;
    if (git_reset(repo.raw(), obj.get(), t, &opts) < 0) return lastError(QStringLiteral("reset"));
    return {};
}

QString commitAsEmail(const Repository &repo, const Oid &id, QString *error) {
    CommitPtr c = repo.lookupCommit(id);
    if (!c) {
        if (error) *error = QStringLiteral("commit not found");
        return {};
    }
    BufGuard buf;
    git_email_create_options opts = GIT_EMAIL_CREATE_OPTIONS_INIT;
    opts.flags = GIT_EMAIL_CREATE_OMIT_NUMBERS;
    if (git_email_create_from_commit(&buf.buf, c.get(), &opts) < 0) {
        if (error) *error = lastGitError(QStringLiteral("format patch"));
        git_error_clear();
        return {};
    }
    return QString::fromUtf8(buf.bytes());
}

QString makePatch(const Repository &repo, const Oid &from, const Oid &to, QString *error) {
    if (!to.isPseudo()) {
        CommitPtr c = repo.lookupCommit(to);
        if (c && git_commit_parentcount(c.get()) == 1 && Oid(git_commit_parent_id(c.get(), 0)) == from)
            return commitAsEmail(repo, to, error);
    }
    DiffOptions o;
    DiffResult d = DiffEngine::diffBetween(repo, from, to, o);
    if (!d.error.isEmpty()) {
        if (error) *error = d.error;
        return {};
    }
    QString out;
    for (const FileDiff &f : d.files) out += f.lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    return out;
}

} // namespace gitq::Ops
