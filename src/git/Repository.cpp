#include "git/Repository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace gitq {

QList<RefInfo> RefSet::refsFor(const Oid &id) const {
    QList<RefInfo> out;
    for (auto it = byCommit.constFind(id); it != byCommit.cend() && it.key() == id; ++it)
        out.prepend(refs.at(it.value()));
    return out;
}

Person personFrom(const git_signature *sig, QSet<QString> *pool) {
    Person p;
    if (!sig) return p;
    auto intern = [pool](const char *s) {
        QString str = QString::fromUtf8(s);
        if (!pool) return str;
        auto it = pool->constFind(str);
        if (it != pool->cend()) return *it;
        pool->insert(str);
        return str;
    };
    p.name = intern(sig->name);
    p.email = intern(sig->email);
    p.time = sig->when.time;
    p.offset = sig->when.offset;
    return p;
}

Repository Repository::open(const QString &path) {
    BufGuard root;
    QByteArray p = QDir(path).absolutePath().toUtf8();
    check(git_repository_discover(&root.buf, p.constData(), 0, nullptr), QStringLiteral("not a git repository"));
    git_repository *r = nullptr;
    check(git_repository_open(&r, root.buf.ptr), QStringLiteral("cannot open repository"));
    return wrap(r);
}

Repository Repository::wrap(git_repository *r) {
    Repository repo;
    repo.m_repo.reset(r);
    repo.m_path = QString::fromUtf8(git_repository_path(r));
    return repo;
}

Repository Repository::openSubmodule(const QString &path) const {
    const QByteArray p = path.toUtf8();
    QString name = path;
    git_submodule *sm = nullptr;
    if (git_submodule_lookup(&sm, m_repo.get(), p.constData()) == 0) {
        name = QString::fromUtf8(git_submodule_name(sm));
        git_repository *r = nullptr;
        const int rc = git_submodule_open(&r, sm);
        git_submodule_free(sm);
        if (rc == 0) return wrap(r);
    }
    git_error_clear();
    // Not checked out (or no longer in .gitmodules): its repository may still be in .git/modules.
    const QString dir = QDir(m_path).filePath(QStringLiteral("modules/") + name);
    if (!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("HEAD")))) return {};
    git_repository *r = nullptr;
    if (git_repository_open_ext(&r, QFile::encodeName(dir).constData(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr) < 0) {
        git_error_clear();
        return {};
    }
    return wrap(r);
}

unsigned Repository::submoduleStatus(const QString &path) const {
    git_submodule *sm = nullptr;
    if (git_submodule_lookup(&sm, m_repo.get(), path.toUtf8().constData()) < 0) {
        git_error_clear();
        return 0;
    }
    unsigned st = 0;
    if (git_submodule_status(&st, m_repo.get(), git_submodule_name(sm), GIT_SUBMODULE_IGNORE_UNSPECIFIED) < 0) {
        git_error_clear();
        st = 0;
    }
    git_submodule_free(sm);
    return st;
}

QString Repository::workdir() const {
    const char *w = git_repository_workdir(m_repo.get());
    return w ? QString::fromUtf8(w) : QString();
}

bool Repository::isBare() const { return git_repository_is_bare(m_repo.get()); }

void Repository::upstreamOf(git_reference *branch, RefInfo &info) const {
    git_reference *up = nullptr;
    if (git_branch_upstream(&up, branch) < 0) {
        git_error_clear();
        return;
    }
    RefPtr guard(up);
    git_object *peeled = nullptr;
    if (git_reference_peel(&peeled, up, GIT_OBJECT_COMMIT) < 0) {
        git_error_clear();
        return;
    }
    const Oid upCommit(git_object_id(peeled));
    git_object_free(peeled);
    info.upstream = QString::fromUtf8(git_reference_shorthand(up));
    size_t ahead = 0, behind = 0;
    if (upCommit != info.commit &&
        git_graph_ahead_behind(&ahead, &behind, m_repo.get(), info.commit.ptr(), upCommit.ptr()) == 0) {
        info.ahead = int(ahead);
        info.behind = int(behind);
    }
    git_error_clear();
}

RefSet Repository::loadRefs() const {
    RefSet set;
    git_reference_iterator *it = nullptr;
    if (git_reference_iterator_new(&it, m_repo.get()) < 0) return set;
    RefIterPtr iter(it);
    git_reference *ref = nullptr;
    while (git_reference_next(&ref, iter.get()) == 0) {
        RefPtr guard(ref);
        RefInfo info;
        info.fullName = QString::fromUtf8(git_reference_name(ref));
        if (info.fullName.startsWith(QLatin1String("refs/heads/"))) {
            info.kind = RefInfo::LocalBranch;
            info.name = info.fullName.mid(11);
        } else if (info.fullName.startsWith(QLatin1String("refs/remotes/"))) {
            info.kind = RefInfo::RemoteBranch;
            info.name = info.fullName.mid(13);
            if (info.name.endsWith(QLatin1String("/HEAD"))) continue;   // gitk hides these too
        } else if (info.fullName.startsWith(QLatin1String("refs/tags/"))) {
            info.kind = RefInfo::Tag;
            info.name = info.fullName.mid(10);
        } else if (info.fullName.startsWith(QLatin1String("refs/stash"))) {
            continue;
        } else {
            info.kind = RefInfo::Other;
            info.name = info.fullName.startsWith(QLatin1String("refs/")) ? info.fullName.mid(5) : info.fullName;
        }
        git_reference *resolved = nullptr;
        if (git_reference_resolve(&resolved, ref) < 0) continue;
        RefPtr rguard(resolved);
        const git_oid *target = git_reference_target(resolved);
        if (!target) continue;
        info.target = Oid(target);
        git_object *peeled = nullptr;
        if (git_reference_peel(&peeled, resolved, GIT_OBJECT_COMMIT) < 0) {
            git_error_clear();
            continue;   // e.g. tag pointing at a tree or blob
        }
        info.commit = Oid(git_object_id(peeled));
        git_object_free(peeled);
        if (info.kind == RefInfo::LocalBranch) upstreamOf(ref, info);
        set.refs.append(info);
        set.byCommit.insert(info.commit, int(set.refs.size() - 1));
    }

    git_reference *head = nullptr;
    if (git_repository_head(&head, m_repo.get()) == 0) {
        RefPtr guard(head);
        if (git_reference_is_branch(head))
            set.headBranch = QString::fromUtf8(git_reference_shorthand(head));
        git_object *obj = nullptr;
        if (git_reference_peel(&obj, head, GIT_OBJECT_COMMIT) == 0) {
            set.head = Oid(git_object_id(obj));
            git_object_free(obj);
        }
    } else {
        git_error_clear();
    }
    return set;
}

Oid Repository::resolveCommit(const QString &spec, QString *error) const {
    git_object *obj = nullptr;
    QByteArray s = spec.toUtf8();
    if (git_revparse_single(&obj, m_repo.get(), s.constData()) < 0) {
        if (error) *error = lastGitError(spec);
        git_error_clear();
        return Oid();
    }
    ObjectPtr guard(obj);
    git_object *peeled = nullptr;
    if (git_object_peel(&peeled, obj, GIT_OBJECT_COMMIT) < 0) {
        if (error) *error = lastGitError(spec);
        git_error_clear();
        return Oid();
    }
    Oid r(git_object_id(peeled));
    git_object_free(peeled);
    return r;
}

CommitPtr Repository::lookupCommit(const Oid &id) const {
    git_commit *c = nullptr;
    if (git_commit_lookup(&c, m_repo.get(), id.ptr()) < 0) {
        git_error_clear();
        return {};
    }
    return CommitPtr(c);
}

void Repository::fillCommitInfo(git_commit *c, CommitInfo &info, QSet<QString> *pool) {
    info.id = Oid(git_commit_id(c));
    info.parents.clear();
    unsigned n = git_commit_parentcount(c);
    for (unsigned i = 0; i < n; ++i) info.parents.append(Oid(git_commit_parent_id(c, i)));
    info.author = personFrom(git_commit_author(c), pool);
    info.committer = personFrom(git_commit_committer(c), pool);
    const char *msg = git_commit_message(c);
    info.rawMessage = QByteArray(msg ? msg : "");
    int nl = int(info.rawMessage.indexOf('\n'));
    info.subject = QString::fromUtf8(nl < 0 ? info.rawMessage : info.rawMessage.left(nl));
}

CommitInfo Repository::readCommit(const Oid &id) const {
    CommitInfo info;
    if (auto c = lookupCommit(id)) fillCommitInfo(c.get(), info);
    return info;
}

Oid Repository::mergeBase(const Oid &a, const Oid &b) const {
    git_oid out;
    if (git_merge_base(&out, m_repo.get(), a.ptr(), b.ptr()) < 0) {
        git_error_clear();
        return Oid();
    }
    return Oid(out);
}

bool Repository::isAncestor(const Oid &ancestor, const Oid &descendant) const {
    if (ancestor == descendant) return true;
    return git_graph_descendant_of(m_repo.get(), descendant.ptr(), ancestor.ptr()) == 1;
}

void Repository::localChanges(bool *indexChanged, bool *workdirChanged, bool untracked) const {
    *indexChanged = *workdirChanged = false;
    if (isBare()) return;
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    // Untracked files are not "changes" for gitk unless asked for. Submodules count when they point
    // at another commit or have modified content (as in `git status`; their `ignore` setting applies).
    opts.flags = untracked ? GIT_STATUS_OPT_INCLUDE_UNTRACKED : 0;
    git_status_list *list = nullptr;
    if (git_status_list_new(&list, m_repo.get(), &opts) < 0) {
        git_error_clear();
        return;
    }
    StatusListPtr guard(list);
    size_t n = git_status_list_entrycount(list);
    for (size_t i = 0; i < n; ++i) {
        const git_status_entry *e = git_status_byindex(list, i);
        if (e->status & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED |
                         GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE))
            *indexChanged = true;
        if (e->status & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED | GIT_STATUS_WT_TYPECHANGE |
                         GIT_STATUS_WT_RENAMED | GIT_STATUS_CONFLICTED | (untracked ? GIT_STATUS_WT_NEW : 0)))
            *workdirChanged = true;
    }
}

QString Repository::configString(const QString &key, const QString &def) const {
    git_config *cfg = nullptr;
    if (git_repository_config_snapshot(&cfg, m_repo.get()) < 0) {
        git_error_clear();
        return def;
    }
    ConfigPtr guard(cfg);
    const char *val = nullptr;
    if (git_config_get_string(&val, cfg, key.toUtf8().constData()) < 0) {
        git_error_clear();
        return def;
    }
    return QString::fromUtf8(val);
}

} // namespace gitq
