#include "git/NearbyRefs.h"

#include <QHash>
#include <QQueue>
#include <QSet>
#include <algorithm>

namespace gitq::Nearby {

NearbyRefs compute(const Repository &repo, const Oid &id, const RefSet &refs, const std::atomic<bool> *cancel) {
    NearbyRefs r;
    r.id = id;
    if (id.isPseudo()) return r;
    auto stop = [&] { return cancel && cancel->load(); };

    CommitPtr self = repo.lookupCommit(id);
    if (!self) return r;
    const qint64 selfTime = git_commit_time(self.get());

    QMultiHash<Oid, QString> tagsByCommit;
    for (const RefInfo &ref : refs.refs) {
        if (ref.kind == RefInfo::Tag) tagsByCommit.insert(ref.commit, ref.name);
    }

    // Branches containing the commit.
    for (const RefInfo &ref : refs.refs) {
        if (stop()) return r;
        if (ref.kind != RefInfo::LocalBranch) continue;
        if (ref.commit == id || repo.isAncestor(id, ref.commit)) r.branches << ref.name;
    }

    // Follows: BFS through ancestors, stopping at tagged commits.
    {
        QQueue<Oid> queue;
        QSet<Oid> seen;
        for (unsigned i = 0; i < git_commit_parentcount(self.get()); ++i) {
            Oid p(git_commit_parent_id(self.get(), i));
            if (!seen.contains(p)) {
                seen.insert(p);
                queue.enqueue(p);
            }
        }
        int budget = 200000;
        while (!queue.isEmpty() && budget-- > 0) {
            if (stop()) return r;
            Oid c = queue.dequeue();
            if (tagsByCommit.contains(c)) {
                for (auto it = tagsByCommit.constFind(c); it != tagsByCommit.cend() && it.key() == c; ++it)
                    if (!r.follows.contains(it.value())) r.follows << it.value();
                continue;
            }
            CommitPtr cc = repo.lookupCommit(c);
            if (!cc) continue;
            for (unsigned i = 0; i < git_commit_parentcount(cc.get()); ++i) {
                Oid p(git_commit_parent_id(cc.get(), i));
                if (!seen.contains(p)) {
                    seen.insert(p);
                    queue.enqueue(p);
                }
            }
        }
    }

    // Precedes: tagged descendants not preceded by another tagged descendant.
    {
        QList<Oid> candidates;
        QSet<Oid> considered;
        for (const RefInfo &ref : refs.refs) {
            if (stop()) return r;
            if (ref.kind != RefInfo::Tag || considered.contains(ref.commit)) continue;
            considered.insert(ref.commit);
            if (ref.commit == id) continue;
            CommitPtr tc = repo.lookupCommit(ref.commit);
            if (!tc || git_commit_time(tc.get()) < selfTime - 86400) continue;
            if (repo.isAncestor(id, ref.commit)) candidates << ref.commit;
        }
        for (const Oid &c : candidates) {
            if (stop()) return r;
            bool nearest = true;
            for (const Oid &o : candidates)
                if (o != c && repo.isAncestor(o, c)) {
                    nearest = false;
                    break;
                }
            if (!nearest) continue;
            for (auto it = tagsByCommit.constFind(c); it != tagsByCommit.cend() && it.key() == c; ++it)
                r.precedes << it.value();
        }
    }
    std::sort(r.branches.begin(), r.branches.end());
    std::sort(r.follows.begin(), r.follows.end());
    std::sort(r.precedes.begin(), r.precedes.end());
    return r;
}

} // namespace gitq::Nearby
