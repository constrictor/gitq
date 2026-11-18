#include "git/LogLoader.h"

#include "git/Repository.h"

#include <QElapsedTimer>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace gitq {

// ---------------------------------------------------------------------------
// PathFilter

PathFilter::PathFilter(git_repository *repo, const QStringList &paths) : m_repo(repo) {
    for (QString p : paths) {
        while (p.endsWith(QLatin1Char('/'))) p.chop(1);
        if (p.startsWith(QLatin1String("./"))) p = p.mid(2);
        if (p == QLatin1String(".")) p.clear();
        if (p.contains(QLatin1Char('*')) || p.contains(QLatin1Char('?')) || p.contains(QLatin1Char('[')) ||
            p.startsWith(QLatin1Char(':')))
            m_literal = false;
        m_paths << p;
    }
    if (m_paths.contains(QString())) {   // whole tree
        m_paths = {QString()};
        m_literal = true;
    }
    if (!m_literal) {
        QList<QByteArray> storage;
        for (const QString &p : m_paths) storage << p.toUtf8();
        QList<char *> ptrs;
        for (QByteArray &b : storage) ptrs << b.data();
        git_strarray arr{ptrs.data(), size_t(ptrs.size())};
        git_pathspec_new(&m_spec, &arr);
    }
}

PathFilter::~PathFilter() { git_pathspec_free(m_spec); }

bool PathFilter::matchesPath(const QString &path) const {
    if (m_paths.isEmpty()) return true;
    if (m_spec) {
        QByteArray b = path.toUtf8();
        return git_pathspec_matches_path(m_spec, 0, b.constData()) == 1;
    }
    for (const QString &p : m_paths) {
        if (p.isEmpty() || path == p || path.startsWith(p + QLatin1Char('/'))) return true;
    }
    return false;
}

bool PathFilter::treeSame(git_tree *a, git_tree *b) const {
    if (m_paths.isEmpty()) {
        if (!a || !b) return a == b;
        return git_oid_equal(git_tree_id(a), git_tree_id(b));
    }
    if (a && b && git_oid_equal(git_tree_id(a), git_tree_id(b))) return true;
    if (m_literal) {
        for (const QString &p : m_paths) {
            if (p.isEmpty()) {
                if (!a || !b) return false;
                continue;   // ids differ (checked above)
            }
            QByteArray path = p.toUtf8();
            git_tree_entry *ea = nullptr, *eb = nullptr;
            if (a && git_tree_entry_bypath(&ea, a, path.constData()) < 0) ea = nullptr;
            if (b && git_tree_entry_bypath(&eb, b, path.constData()) < 0) eb = nullptr;
            git_error_clear();
            bool same;
            if (!ea || !eb)
                same = (ea == eb);
            else
                same = git_oid_equal(git_tree_entry_id(ea), git_tree_entry_id(eb)) &&
                       git_tree_entry_filemode(ea) == git_tree_entry_filemode(eb);
            git_tree_entry_free(ea);
            git_tree_entry_free(eb);
            if (!same) return false;
        }
        return true;
    }
    QList<QByteArray> storage;
    for (const QString &p : m_paths) storage << p.toUtf8();
    QList<char *> ptrs;
    for (QByteArray &s : storage) ptrs << s.data();
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec = {ptrs.data(), size_t(ptrs.size())};
    git_diff *diff = nullptr;
    if (git_diff_tree_to_tree(&diff, m_repo, a, b, &opts) < 0) {
        git_error_clear();
        return false;
    }
    bool same = git_diff_num_deltas(diff) == 0;
    git_diff_free(diff);
    return same;
}

// ---------------------------------------------------------------------------
// Log loading

namespace {

struct Matchers {
    QList<QRegularExpression> authors, committers, greps;
    bool allMatch = false;

    explicit Matchers(const LogOptions &o) {
        auto opts = o.regexIgnoreCase ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption;
        auto make = [&](const QString &pat) {
            return QRegularExpression(o.fixedStrings ? QRegularExpression::escape(pat) : pat, opts);
        };
        for (const QString &s : o.authors) authors << make(s);
        for (const QString &s : o.committers) committers << make(s);
        for (const QString &s : o.greps) greps << make(s);
        allMatch = o.allMatch;
    }
    bool empty() const { return authors.isEmpty() && committers.isEmpty() && greps.isEmpty(); }

    bool matches(const CommitInfo &c) const {
        auto any = [](const QList<QRegularExpression> &res, const QString &text) {
            if (res.isEmpty()) return true;
            for (const auto &re : res)
                if (re.match(text).hasMatch()) return true;
            return false;
        };
        if (!any(authors, c.author.nameEmail())) return false;
        if (!any(committers, c.committer.nameEmail())) return false;
        if (greps.isEmpty()) return true;
        const QString message = c.message();
        if (allMatch) {
            for (const auto &re : greps)
                if (!re.match(message).hasMatch()) return false;
            return true;
        }
        return any(greps, message);
    }
};

bool passesCommitFilters(const LogOptions &o, const Matchers &m, const CommitInfo &c) {
    int np = int(c.parents.size());
    if (np < o.minParents) return false;
    if (o.maxParents >= 0 && np > o.maxParents) return false;
    if (o.since && c.committer.time < o.since) return false;
    if (o.until && c.committer.time > o.until) return false;
    return m.matches(c);
}

TreePtr commitTree(git_commit *c) {
    git_tree *t = nullptr;
    if (git_commit_tree(&t, c) < 0) {
        git_error_clear();
        return {};
    }
    return TreePtr(t);
}

TreePtr treeOf(git_repository *repo, const Oid &id) {
    git_commit *c = nullptr;
    if (git_commit_lookup(&c, repo, id.ptr()) < 0) {
        git_error_clear();
        return {};
    }
    CommitPtr guard(c);
    return commitTree(c);
}

class Emitter {
public:
    Emitter(const std::function<void(CommitPtrList &&)> &sink, int batchSize) : m_sink(sink), m_batchSize(batchSize) {
        m_timer.start();
    }
    void add(QSharedPointer<CommitInfo> c) {
        m_pending.append(std::move(c));
        if (m_pending.size() >= m_batchSize || (m_pending.size() >= 50 && m_timer.elapsed() > 100)) flush();
    }
    void flush() {
        if (m_pending.isEmpty()) return;
        m_sink(std::move(m_pending));
        m_pending = {};
        m_timer.restart();
    }

private:
    const std::function<void(CommitPtrList &&)> &m_sink;
    int m_batchSize;
    CommitPtrList m_pending;
    QElapsedTimer m_timer;
};

} // namespace

QString loadLog(const Repository &repo, const LogOptions &o, const std::function<void(CommitPtrList &&)> &sink,
                const std::atomic<bool> *cancel, int batchSize) {
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };

    QString error;
    ResolvedRevs revs = RevArgs::resolve(repo, o, &error);
    if (revs.include.isEmpty()) return error;   // empty repo or bad revisions

    git_revwalk *w = nullptr;
    if (git_revwalk_new(&w, repo.raw()) < 0) return lastGitError();
    RevwalkPtr walk(w);

    const bool filtered = !o.paths.isEmpty() || o.minParents > 0 || o.maxParents >= 0 || o.since || o.until ||
                          !o.authors.isEmpty() || !o.committers.isEmpty() || !o.greps.isEmpty();
    unsigned sort = GIT_SORT_NONE;
    if (o.order == LogOptions::TopoOrder) sort = GIT_SORT_TOPOLOGICAL;
    else if (o.order == LogOptions::DateOrder || filtered) sort = GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME;
    else sort = GIT_SORT_TIME;
    git_revwalk_sorting(w, sort);
    if (o.firstParent) git_revwalk_simplify_first_parent(w);

    for (const Oid &id : revs.include)
        if (git_revwalk_push(w, id.ptr()) < 0) git_error_clear();
    for (const Oid &id : revs.exclude)
        if (git_revwalk_hide(w, id.ptr()) < 0) git_error_clear();

    Emitter out(sink, batchSize);
    QSet<QString> namePool;
    const bool wantBoundary = o.boundary && !revs.exclude.isEmpty();
    QSet<Oid> walked;
    QList<Oid> boundaryOrder;
    QSet<Oid> boundarySet;
    int skip = o.skip;
    int remaining = o.maxCount;

    auto emitCommit = [&](QSharedPointer<CommitInfo> info) -> bool {
        if (skip > 0) {
            --skip;
            return true;
        }
        if (remaining == 0) return false;
        if (remaining > 0) --remaining;
        if (wantBoundary)
            for (const Oid &p : info->parents)
                if (!boundarySet.contains(p)) {
                    boundarySet.insert(p);
                    boundaryOrder.append(p);
                }
        out.add(std::move(info));
        return true;
    };

    if (o.showLocalChanges && !repo.isBare()) {
        Oid head = repo.resolveCommit(QStringLiteral("HEAD"));
        bool inView = false;
        for (const Oid &inc : revs.include) inView = inView || repo.isAncestor(head, inc);
        for (const Oid &exc : revs.exclude) inView = inView && !repo.isAncestor(head, exc);
        bool indexChanged = false, workdirChanged = false;
        if (inView && !head.isNull()) repo.localChanges(&indexChanged, &workdirChanged);
        auto pseudo = [&](const Oid &id, const Oid &parent, const QString &subject) {
            auto info = QSharedPointer<CommitInfo>::create();
            info->id = id;
            info->parents = {parent};
            info->subject = subject;
            info->rawMessage = subject.toUtf8();
            out.add(std::move(info));
        };
        if (workdirChanged)
            pseudo(Oid::workdirId(), indexChanged ? Oid::indexId() : head,
                   QStringLiteral("Local uncommitted changes, not checked in to index"));
        if (indexChanged)
            pseudo(Oid::indexId(), head, QStringLiteral("Local changes checked in to index but not committed"));
    }

    git_oid oid;
    if (!filtered) {
        // Streaming mode: every walked commit is shown with its real parents.
        int rc;
        while ((rc = git_revwalk_next(&oid, w)) == 0) {
            if (cancelled()) return QStringLiteral("cancelled");
            git_commit *c = nullptr;
            if (git_commit_lookup(&c, repo.raw(), &oid) < 0) {
                git_error_clear();
                continue;
            }
            CommitPtr guard(c);
            auto info = QSharedPointer<CommitInfo>::create();
            Repository::fillCommitInfo(c, *info, &namePool);
            if (o.firstParent && info->parents.size() > 1) info->parents.resize(1);
            if (wantBoundary) walked.insert(info->id);
            if (!emitCommit(std::move(info))) break;
        }
        if (rc < 0 && rc != GIT_ITEROVER) error = lastGitError(QStringLiteral("revision walk"));
    } else {
        // Filtered mode: collect the whole walk, simplify history, rewrite parents.
        PathFilter paths(repo.raw(), o.paths);
        Matchers matchers(o);
        struct Node {
            QSharedPointer<CommitInfo> info;
            QList<Oid> simplParents;
            bool live = false;
            bool shown = false;
        };
        QList<Node> nodes;
        QHash<Oid, int> index;
        QSet<Oid> tips(revs.include.cbegin(), revs.include.cend());

        int rc;
        while ((rc = git_revwalk_next(&oid, w)) == 0) {
            if (cancelled()) return QStringLiteral("cancelled");
            git_commit *c = nullptr;
            if (git_commit_lookup(&c, repo.raw(), &oid) < 0) {
                git_error_clear();
                continue;
            }
            CommitPtr guard(c);
            Node n;
            n.info = QSharedPointer<CommitInfo>::create();
            Repository::fillCommitInfo(c, *n.info, &namePool);
            if (o.firstParent && n.info->parents.size() > 1) n.info->parents.resize(1);
            index.insert(n.info->id, int(nodes.size()));
            nodes.append(std::move(n));
        }
        if (rc < 0 && rc != GIT_ITEROVER) error = lastGitError(QStringLiteral("revision walk"));

        // Forward pass (children before parents): liveness, simplification, visibility.
        for (Node &n : nodes) {
            if (cancelled()) return QStringLiteral("cancelled");
            if (tips.contains(n.info->id)) n.live = true;
            const QList<Oid> &parents = n.info->parents;
            bool touches = true;
            n.simplParents = parents;
            if (n.live && !paths.isEmpty()) {
                CommitPtr c = repo.lookupCommit(n.info->id);
                TreePtr tree = c ? commitTree(c.get()) : TreePtr();
                if (parents.isEmpty()) {
                    touches = !paths.treeSame(tree.get(), nullptr);
                } else if (parents.size() == 1) {
                    TreePtr pt = treeOf(repo.raw(), parents.first());
                    touches = !paths.treeSame(tree.get(), pt.get());
                } else {
                    int sameAs = -1;
                    int sameCount = 0;
                    for (int i = 0; i < parents.size(); ++i) {
                        TreePtr pt = treeOf(repo.raw(), parents.at(i));
                        if (paths.treeSame(tree.get(), pt.get())) {
                            ++sameCount;
                            if (sameAs < 0) sameAs = i;
                        }
                    }
                    if (o.fullHistory) {
                        touches = sameCount < parents.size();
                    } else if (sameAs >= 0) {
                        touches = false;
                        n.simplParents = {parents.at(sameAs)};
                    }
                }
            }
            if (n.live) {
                n.shown = touches && passesCommitFilters(o, matchers, *n.info);
                for (const Oid &p : n.simplParents) {
                    auto it = index.constFind(p);
                    if (it != index.cend()) nodes[*it].live = true;
                }
            }
        }

        // Backward pass (parents before children): rewrite parents to shown ancestors.
        QHash<Oid, QList<Oid>> reps;   // non-shown commit -> nearest shown ancestors
        auto repsOf = [&](const Oid &p, QList<Oid> &acc) {
            auto it = index.constFind(p);
            if (it == index.cend()) {   // outside the walk (hidden): keep as is
                if (!acc.contains(p)) acc.append(p);
                return;
            }
            const Node &pn = nodes.at(*it);
            if (pn.shown) {
                if (!acc.contains(p)) acc.append(p);
            } else {
                for (const Oid &r : reps.value(p))
                    if (!acc.contains(r)) acc.append(r);
            }
        };
        for (int i = int(nodes.size()) - 1; i >= 0; --i) {
            if (cancelled()) return QStringLiteral("cancelled");
            Node &n = nodes[i];
            if (!n.live) continue;
            QList<Oid> acc;
            for (const Oid &p : n.simplParents) repsOf(p, acc);
            if (n.shown) n.info->parents = acc;
            else reps.insert(n.info->id, acc);
        }

        for (Node &n : nodes) {
            if (!n.shown) continue;
            walked.insert(n.info->id);
            if (!emitCommit(n.info)) break;
        }
    }

    // Boundary commits: parents of shown commits that were excluded by the range.
    if (wantBoundary && !cancelled()) {
        for (const Oid &id : boundaryOrder) {
            if (walked.contains(id)) continue;
            auto info = QSharedPointer<CommitInfo>::create(repo.readCommit(id));
            if (info->id.isNull()) continue;
            info->boundary = true;
            info->parents.clear();   // boundary commits are drawn without their history
            out.add(std::move(info));
        }
    }
    out.flush();
    return error;
}

// ---------------------------------------------------------------------------
// LogLoader

LogLoader::LogLoader(QObject *parent) : QObject(parent) {
    qRegisterMetaType<gitq::CommitPtrList>();
}

LogLoader::~LogLoader() {
    cancel();
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

void LogLoader::cancel() {
    if (m_cancel) m_cancel->store(true);
}

bool LogLoader::isRunning() const { return m_thread && m_thread->isRunning(); }

void LogLoader::start(const QString &repoPath, const LogOptions &opts) {
    cancel();
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const int gen = ++m_generation;
    auto cancelFlag = m_cancel;
    LogLoader *self = this;   // outlives the thread: the destructor waits for it

    m_thread = QThread::create([=] {
        QString err;
        try {
            Repository repo = Repository::open(repoPath);
            err = loadLog(repo, opts, [&](CommitPtrList &&batch) {
                if (cancelFlag->load()) return;
                QMetaObject::invokeMethod(self, [self, gen, batch = std::move(batch)] {
                    if (self->m_generation == gen) emit self->commitsLoaded(batch);
                }, Qt::QueuedConnection);
            }, cancelFlag.get());
        } catch (const GitError &e) {
            err = e.message();
        }
        if (cancelFlag->load()) return;
        QMetaObject::invokeMethod(self, [self, gen, err] {
            if (self->m_generation == gen) emit self->finished(err);
        }, Qt::QueuedConnection);
    });
    m_thread->start();
}

} // namespace gitq
