#include "git/DiffEngine.h"

#include "git/GitPtr.h"
#include "git/Repository.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <algorithm>
#include <vector>

namespace gitq {

void FileDiff::countChanges(int *added, int *removed) const {
    const int n = qMax(1, numParents);
    int a = 0, r = 0;
    bool hunks = false;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("@@"))) {
            hunks = true;
            continue;
        }
        if (!hunks || line.startsWith(QLatin1Char('\\'))) continue;
        const QString marks = line.left(n);
        if (marks.contains(QLatin1Char('+'))) ++a;
        else if (marks.contains(QLatin1Char('-'))) ++r;
    }
    *added = a;
    *removed = r;
}

QList<FileDiff::Hunk> FileDiff::hunks() const {
    QList<Hunk> out;
    for (int i = 0; i < lines.size(); ++i) {
        if (!lines[i].startsWith(QLatin1String("@@"))) continue;
        if (!out.isEmpty()) out.last().end = i;
        out.append({i, int(lines.size())});
    }
    return out;
}

namespace {

// Holds a git_strarray built from a QStringList.
struct StrArray {
    QList<QByteArray> storage;
    QList<char *> ptrs;
    git_strarray arr{nullptr, 0};
    explicit StrArray(const QStringList &list) {
        for (const QString &s : list) storage << s.toUtf8();
        for (QByteArray &b : storage) ptrs << b.data();
        arr = {ptrs.data(), size_t(ptrs.size())};
    }
};

git_diff_options makeOptions(const DiffOptions &o, StrArray &spec, bool reverse = false) {
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.context_lines = uint32_t(qMax(0, o.context));
    opts.interhunk_lines = 0;
    if (o.ignoreSpaceChange) opts.flags |= GIT_DIFF_IGNORE_WHITESPACE_CHANGE;
    if (reverse) opts.flags |= GIT_DIFF_REVERSE;
    opts.pathspec = spec.arr;
    return opts;
}

QStringList splitText(const QByteArray &data) {
    QString s = QString::fromUtf8(data);
    QStringList lines = s.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines;
}

QChar statusChar(git_delta_t s) {
    switch (s) {
    case GIT_DELTA_ADDED: return QLatin1Char('A');
    case GIT_DELTA_DELETED: return QLatin1Char('D');
    case GIT_DELTA_RENAMED: return QLatin1Char('R');
    case GIT_DELTA_COPIED: return QLatin1Char('C');
    case GIT_DELTA_TYPECHANGE: return QLatin1Char('T');
    case GIT_DELTA_UNTRACKED: return QLatin1Char('?');
    case GIT_DELTA_CONFLICTED: return QLatin1Char('U');
    default: return QLatin1Char('M');
    }
}

// ---- Submodules ----------------------------------------------------------

constexpr int kMaxSubmoduleDepth = 8;        // nesting of submodules expanded into a diff
constexpr int kMaxSubmoduleCommits = 100;    // commits listed per direction
constexpr int kMaxSubmoduleCount = 100000;   // commits counted beyond the list

struct Sides {
    bool oldWorkdir = false;   // the old / new side of the diff is the work tree
    bool newWorkdir = false;
};

DiffResult diffBetweenImpl(const Repository &repo, const Oid &from, const Oid &to, const DiffOptions &o, int depth);

bool hasGlob(const QString &p) {
    return p.contains(QLatin1Char('*')) || p.contains(QLatin1Char('?')) || p.contains(QLatin1Char('[')) ||
           p.startsWith(QLatin1Char(':'));
}

// Leading parts of `path`, shortest first: "a", "a/b", "a/b/c".
QStringList pathPrefixes(const QString &path) {
    QStringList out;
    QString cur;
    for (const QString &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        cur = cur.isEmpty() ? part : cur + QLatin1Char('/') + part;
        out << cur;
    }
    return out;
}

// Whether `path` or a leading part of it is a submodule in `tree`; gives that part and its commit.
bool gitlinkIn(git_tree *tree, const QString &path, QString *prefix, Oid *commit) {
    if (!tree) return false;
    for (const QString &cur : pathPrefixes(path)) {
        git_tree_entry *e = nullptr;
        if (git_tree_entry_bypath(&e, tree, cur.toUtf8().constData()) < 0) {
            git_error_clear();
            return false;
        }
        TreeEntryPtr guard(e);
        const git_object_t t = git_tree_entry_type(e);
        if (t == GIT_OBJECT_COMMIT) {
            *prefix = cur;
            *commit = Oid(git_tree_entry_id(e));
            return true;
        }
        if (t != GIT_OBJECT_TREE) return false;
    }
    return false;
}

bool gitlinkIn(git_index *idx, const QString &path, QString *prefix, Oid *commit) {
    if (!idx) return false;
    for (const QString &cur : pathPrefixes(path)) {
        const git_index_entry *e = git_index_get_bypath(idx, cur.toUtf8().constData(), 0);
        if (e && e->mode == GIT_FILEMODE_COMMIT) {
            *prefix = cur;
            *commit = Oid(e->id);
            return true;
        }
        if (e) return false;   // a file
    }
    return false;
}

// The pathspec `paths`, plus the submodules that paths inside a submodule pass through (a pathspec
// "sub/src" doesn't match the entry "sub" itself).
QStringList withSubmodulePaths(const QStringList &paths, std::initializer_list<git_tree *> trees, git_index *idx = nullptr) {
    QStringList out = paths;
    for (const QString &p : paths) {
        if (hasGlob(p)) continue;
        QString prefix;
        Oid commit;
        bool found = gitlinkIn(idx, p, &prefix, &commit);
        for (git_tree *t : trees) found = found || gitlinkIn(t, p, &prefix, &commit);
        if (found && prefix != p && !out.contains(prefix)) out << prefix;
    }
    return out;
}

// `paths` (pathspecs or submodule paths) as seen from inside the submodule at `sub`. A pathspec that
// covers the whole submodule yields no limit (an empty list).
QStringList pathsInSubmodule(const QStringList &paths, const QString &sub, bool pathspec) {
    QStringList out;
    const QString pre = sub + QLatin1Char('/');
    for (QString p : paths) {
        while (p.endsWith(QLatin1Char('/'))) p.chop(1);
        if (p.startsWith(QLatin1String("./"))) p = p.mid(2);
        if (pathspec && (p.isEmpty() || p == QLatin1String(".") || p == sub || pre.startsWith(p + QLatin1Char('/'))))
            return {};
        if (p.startsWith(pre)) out << p.mid(pre.size());
    }
    return out;
}

// Commits reachable from `from` but not from `hide`, newest first, at most kMaxSubmoduleCommits.
QList<CommitLink> commitRange(const Repository &repo, const Oid &from, const Oid &hide, int *more) {
    QList<CommitLink> out;
    *more = 0;
    git_revwalk *w = nullptr;
    if (git_revwalk_new(&w, repo.raw()) < 0) {
        git_error_clear();
        return out;
    }
    RevwalkPtr walk(w);
    git_revwalk_sorting(w, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    if (git_revwalk_push(w, from.ptr()) < 0 || git_revwalk_hide(w, hide.ptr()) < 0) {
        git_error_clear();
        return out;
    }
    git_oid id;
    while (git_revwalk_next(&id, w) == 0) {
        if (out.size() >= kMaxSubmoduleCommits) {
            if (++*more >= kMaxSubmoduleCount) break;
            continue;
        }
        CommitPtr c = repo.lookupCommit(Oid(id));
        out.append({Oid(id), c ? QString::fromUtf8(git_commit_summary(c.get())) : QString()});
    }
    git_error_clear();
    return out;
}

// Adds the files of a submodule's diff `inner` to `r`, as paths under `sub`.
void appendSubmoduleFiles(DiffResult &r, DiffResult &&inner, const QString &sub) {
    const int offset = int(r.submodules.size());
    const QString pre = sub + QLatin1Char('/');
    for (SubmoduleChange &s : inner.submodules) {
        s.path = pre + s.path;
        r.submodules.append(std::move(s));
    }
    for (FileDiff &f : inner.files) {
        const QString oldP = pre + (f.oldPath.isEmpty() ? f.newPath : f.oldPath);
        const QString newP = pre + (f.newPath.isEmpty() ? f.oldPath : f.newPath);
        for (QString &l : f.lines) {
            if (l.startsWith(QLatin1String("@@"))) break;
            if (l.startsWith(QLatin1String("diff --git ")))
                l = QStringLiteral("diff --git a/%1 b/%2").arg(oldP, newP);
            else if (l.startsWith(QLatin1String("--- a/")) || l.startsWith(QLatin1String("+++ b/")))
                l.insert(6, pre);
            else
                for (const char *key : {"rename from ", "rename to ", "copy from ", "copy to "})
                    if (l.startsWith(QLatin1String(key))) l.insert(int(qstrlen(key)), pre);
        }
        if (!f.oldPath.isEmpty()) f.oldPath = oldP;
        if (!f.newPath.isEmpty()) f.newPath = newP;
        f.inSubmodule = f.inSubmodule.isEmpty() ? sub : pre + f.inSubmodule;
        if (f.submodule >= 0) f.submodule += offset;
        r.files.append(std::move(f));
    }
}

// Appends the entry `f` for the submodule change `s` to `r`: with the commits between the two
// recorded commits and, when wanted and `canExpand`, followed by the file changes inside it.
void addSubmodule(const Repository &repo, SubmoduleChange s, FileDiff f, DiffResult &r, const DiffOptions &o,
                  int depth, bool canExpand) {
    f.submodule = int(r.submodules.size());
    DiffResult inner;
    if (o.submodules != DiffOptions::SubmoduleSummary) {
        s.detailed = true;
        Repository sub = repo.openSubmodule(s.path);
        if (!sub.isValid()) {
            s.notes << QStringLiteral("submodule not checked out");
        } else {
            s.repoPath = sub.path();
            QStringList missing;
            for (const Oid &id : {s.oldId, s.newId})
                if (!id.isNull() && !sub.lookupCommit(id)) missing << id.shortHex();
            git_error_clear();
            if (!missing.isEmpty()) {
                s.notes << QStringLiteral("commit %1 not present in the submodule (fetch it there)")
                               .arg(missing.join(QStringLiteral(", ")));
                canExpand = false;
            } else if (!s.oldId.isNull() && !s.newId.isNull() && s.oldId != s.newId) {
                s.added = commitRange(sub, s.newId, s.oldId, &s.moreAdded);
                if (!sub.isAncestor(s.oldId, s.newId)) s.removed = commitRange(sub, s.oldId, s.newId, &s.moreRemoved);
            }
            if (s.oldIsWorkdir || s.newIsWorkdir) {
                const unsigned st = repo.submoduleStatus(s.path);
                if (st & (GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED | GIT_SUBMODULE_STATUS_WD_WD_MODIFIED))
                    s.notes << QStringLiteral("modified content");
                if (st & GIT_SUBMODULE_STATUS_WD_UNTRACKED) s.notes << QStringLiteral("untracked content");
            }
            s.expandable = canExpand && !s.oldId.isNull() && !s.newId.isNull() && depth < kMaxSubmoduleDepth;
            const bool wanted = o.submodules == DiffOptions::SubmoduleDiff || o.expandSubmodules.contains(s.path);
            if (s.expandable && wanted) {
                DiffOptions so = o;
                so.paths = pathsInSubmodule(o.paths, s.path, true);
                so.expandSubmodules = pathsInSubmodule(o.expandSubmodules, s.path, false);
                const Oid from = s.oldIsWorkdir ? Oid::workdirId() : s.oldId;
                const Oid to = s.newIsWorkdir ? Oid::workdirId() : s.newId;
                inner = diffBetweenImpl(sub, from, to, so, depth + 1);
                if (!inner.error.isEmpty()) s.notes << inner.error;
                s.expanded = true;
            }
        }
    }
    const QString path = s.path;
    r.submodules.append(std::move(s));
    r.files.append(std::move(f));
    if (!inner.files.isEmpty()) appendSubmoduleFiles(r, std::move(inner), path);
}

DiffResult fromGitDiff(const Repository &repo, git_diff *diff, const DiffOptions &o, Sides sides, int depth) {
    DiffResult r;
    if (o.detectRenames) {
        git_diff_find_options fo = GIT_DIFF_FIND_OPTIONS_INIT;
        fo.flags = GIT_DIFF_FIND_RENAMES;
        git_diff_find_similar(diff, &fo);
    }
    size_t n = git_diff_num_deltas(diff);
    for (size_t i = 0; i < n; ++i) {
        const git_diff_delta *d = git_diff_get_delta(diff, i);
        git_patch *p = nullptr;
        if (git_patch_from_diff(&p, diff, i) < 0) {
            git_error_clear();
            continue;
        }
        PatchPtr patch(p);
        FileDiff f;
        f.oldPath = QString::fromUtf8(d->old_file.path);
        f.newPath = QString::fromUtf8(d->new_file.path);
        if (d->status == GIT_DELTA_DELETED) f.newPath.clear();
        if (d->status == GIT_DELTA_ADDED || d->status == GIT_DELTA_UNTRACKED) f.oldPath.clear();
        f.status = statusChar(d->status);
        f.binary = d->flags & GIT_DIFF_FLAG_BINARY;
        BufGuard buf;
        if (p && git_patch_to_buf(&buf.buf, p) == 0) f.lines = splitText(buf.bytes());
        git_error_clear();

        // A submodule on both sides (or added / deleted); not a file turned into a submodule.
        auto isLink = [](const git_diff_file &df) { return df.mode == GIT_FILEMODE_COMMIT; };
        auto isFile = [](const git_diff_file &df) { return df.mode != 0 && df.mode != GIT_FILEMODE_COMMIT; };
        if ((isLink(d->old_file) || isLink(d->new_file)) && !isFile(d->old_file) && !isFile(d->new_file)) {
            SubmoduleChange s;
            s.path = f.path();
            s.status = f.status;
            if (isLink(d->old_file)) s.oldId = Oid(d->old_file.id);
            if (isLink(d->new_file)) s.newId = Oid(d->new_file.id);
            s.oldIsWorkdir = sides.oldWorkdir;
            s.newIsWorkdir = sides.newWorkdir;
            addSubmodule(repo, std::move(s), std::move(f), r, o, depth, true);
            continue;
        }
        r.files.append(std::move(f));
    }
    return r;
}

TreePtr treeForCommit(const Repository &repo, const Oid &id) {
    CommitPtr c = repo.lookupCommit(id);
    if (!c) return {};
    git_tree *t = nullptr;
    if (git_commit_tree(&t, c.get()) < 0) {
        git_error_clear();
        return {};
    }
    return TreePtr(t);
}

TreePtr headTree(const Repository &repo) {
    Oid head = repo.resolveCommit(QStringLiteral("HEAD"));
    return head.isNull() ? TreePtr() : treeForCommit(repo, head);
}

IndexPtr repoIndex(const Repository &repo) {
    git_index *idx = nullptr;
    if (git_repository_index(&idx, repo.raw()) < 0) {
        git_error_clear();
        return {};
    }
    // The repository keeps its index in memory; git (staging, commits) may have changed the file.
    if (git_index_read(idx, 0) < 0) git_error_clear();
    return IndexPtr(idx);
}

bool looksBinary(const QByteArray &data) {
    return data.left(8000).contains('\0');
}

QByteArray blobFromTree(const Repository &repo, git_tree *tree, const QString &path, bool *found) {
    if (found) *found = false;
    if (!tree) return {};
    git_tree_entry *e = nullptr;
    QByteArray p = path.toUtf8();
    if (git_tree_entry_bypath(&e, tree, p.constData()) < 0) {
        git_error_clear();
        return {};
    }
    TreeEntryPtr guard(e);
    git_blob *b = nullptr;
    if (git_blob_lookup(&b, repo.raw(), git_tree_entry_id(e)) < 0) {
        git_error_clear();
        return {};
    }
    BlobPtr bg(b);
    if (found) *found = true;
    return QByteArray(static_cast<const char *>(git_blob_rawcontent(b)), int(git_blob_rawsize(b)));
}

QString blobIdAt(git_tree *tree, const QString &path) {
    if (!tree) return QStringLiteral("0000000");
    git_tree_entry *e = nullptr;
    QByteArray p = path.toUtf8();
    if (git_tree_entry_bypath(&e, tree, p.constData()) < 0) {
        git_error_clear();
        return QStringLiteral("0000000");
    }
    QString id = Oid(git_tree_entry_id(e)).shortHex();
    git_tree_entry_free(e);
    return id;
}

// ---- Combined diff -------------------------------------------------------

struct HunkCollector {
    struct Line { char origin; int oldNo; int newNo; QByteArray text; };
    struct Hunk { int newStart; int newLines; QList<Line> lines; };
    QList<Hunk> hunks;
};

int hunkCb(const git_diff_delta *, const git_diff_hunk *h, void *payload) {
    auto *hc = static_cast<HunkCollector *>(payload);
    hc->hunks.append({h->new_start, h->new_lines, {}});
    return 0;
}

int lineCb(const git_diff_delta *, const git_diff_hunk *, const git_diff_line *l, void *payload) {
    auto *hc = static_cast<HunkCollector *>(payload);
    if (hc->hunks.isEmpty()) return 0;
    if (l->origin != GIT_DIFF_LINE_ADDITION && l->origin != GIT_DIFF_LINE_DELETION) return 0;
    QByteArray text(l->content, int(l->content_len));
    if (text.endsWith('\n')) text.chop(1);
    hc->hunks.last().lines.append({l->origin, l->old_lineno, l->new_lineno, text});
    return 0;
}

QList<QByteArray> splitBytes(const QByteArray &data) {
    QList<QByteArray> lines = data.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines;
}

} // namespace

FileDiff DiffEngine::combinedFileDiff(const QString &path, const QList<QByteArray> &parents, const QByteArray &result,
                                      const DiffOptions &o, bool dense, const QStringList &parentIds,
                                      const QString &resultId) {
    FileDiff f;
    f.oldPath = f.newPath = path;
    const int n = int(parents.size());
    f.numParents = n;
    f.lines << (dense ? QStringLiteral("diff --cc ") : QStringLiteral("diff --combined ")) + path;
    f.lines << QStringLiteral("index ") + parentIds.join(QLatin1Char(',')) + QStringLiteral("..") + resultId;

    bool binary = looksBinary(result);
    for (const QByteArray &p : parents) binary |= looksBinary(p);
    if (binary) {
        f.binary = true;
        f.lines << QStringLiteral("Binary files differ");
        return f;
    }
    f.lines << QStringLiteral("--- a/") + path << QStringLiteral("+++ b/") + path;

    const QList<QByteArray> rLines = splitBytes(result);
    const int R = int(rLines.size());

    struct Lost {
        QByteArray text;
        quint32 mask = 0;
        QList<int> oldLine;
    };
    QList<QList<bool>> added(n, QList<bool>(R, false));
    QList<QList<int>> pline(n, QList<int>(R + 1, 0));
    QList<QList<Lost>> lost(R + 1);

    git_diff_options dopts = GIT_DIFF_OPTIONS_INIT;
    dopts.context_lines = 0;
    if (o.ignoreSpaceChange) dopts.flags |= GIT_DIFF_IGNORE_WHITESPACE_CHANGE;

    for (int i = 0; i < n; ++i) {
        HunkCollector hc;
        git_diff_buffers(parents[i].constData(), size_t(parents[i].size()), nullptr, result.constData(),
                         size_t(result.size()), nullptr, &dopts, nullptr, nullptr, hunkCb, lineCb, &hc);
        git_error_clear();
        QList<int> delBefore(R + 1, 0);
        for (const auto &h : hc.hunks) {
            int anchor = h.newLines == 0 ? h.newStart : h.newStart - 1;
            anchor = qBound(0, anchor, R);
            QList<Lost> mine;
            for (const auto &l : h.lines) {
                if (l.origin == GIT_DIFF_LINE_ADDITION) {
                    if (l.newNo >= 1 && l.newNo <= R) added[i][l.newNo - 1] = true;
                } else {
                    Lost x;
                    x.text = l.text;
                    x.mask = 1u << i;
                    x.oldLine = QList<int>(n, 0);
                    x.oldLine[i] = l.oldNo;
                    mine.append(x);
                    delBefore[anchor]++;
                }
            }
            if (mine.isEmpty()) continue;
            // Coalesce with lines lost from earlier parents at the same anchor (LCS on text).
            QList<Lost> &cur = lost[anchor];
            const int a = int(cur.size()), b = int(mine.size());
            QList<QList<int>> lcs(a + 1, QList<int>(b + 1, 0));
            for (int x = a - 1; x >= 0; --x)
                for (int y = b - 1; y >= 0; --y)
                    lcs[x][y] = (cur[x].text == mine[y].text && !(cur[x].mask & (1u << i)))
                                    ? lcs[x + 1][y + 1] + 1
                                    : qMax(lcs[x + 1][y], lcs[x][y + 1]);
            QList<Lost> merged;
            int x = 0, y = 0;
            while (x < a || y < b) {
                if (x < a && y < b && cur[x].text == mine[y].text && !(cur[x].mask & (1u << i)) &&
                    lcs[x][y] == lcs[x + 1][y + 1] + 1) {
                    Lost m = cur[x];
                    m.mask |= 1u << i;
                    m.oldLine[i] = mine[y].oldLine[i];
                    merged.append(m);
                    ++x, ++y;
                } else if (x < a && (y >= b || lcs[x + 1][y] >= lcs[x][y + 1])) {
                    merged.append(cur[x++]);
                } else {
                    merged.append(mine[y++]);
                }
            }
            cur = merged;
        }
        int p = 1;
        for (int k = 0; k <= R; ++k) {
            p += delBefore[k];
            pline[i][k] = p;
            if (k < R && !added[i][k]) ++p;
        }
    }

    // Element sequence: lost lines anchored at k, then result line k.
    struct Elem { bool isLost; int k; int li; };
    QList<Elem> elems;
    for (int k = 0; k <= R; ++k) {
        for (int li = 0; li < lost[k].size(); ++li) elems.append({true, k, li});
        if (k < R) elems.append({false, k, -1});
    }
    const int ctx = qMax(0, o.context);
    QList<bool> showLine(R, false);
    for (int k = 0; k <= R; ++k) {
        bool changed = !lost[k].isEmpty();
        if (changed)
            for (int j = qMax(0, k - ctx); j < qMin(R, k + ctx); ++j) showLine[j] = true;
        if (k < R) {
            bool add = false;
            for (int i = 0; i < n; ++i) add |= added[i][k];
            if (add)
                for (int j = qMax(0, k - ctx); j <= qMin(R - 1, k + ctx); ++j) showLine[j] = true;
        }
    }
    auto shown = [&](const Elem &e) { return e.isLost || showLine[e.k]; };

    const QString at(n + 1, QLatin1Char('@'));
    int e = 0;
    while (e < elems.size()) {
        if (!shown(elems[e])) {
            ++e;
            continue;
        }
        int s = e;
        while (e < elems.size() && shown(elems[e])) ++e;
        // Hunk elems[s, e)
        QList<bool> touched(n, false);
        for (int j = s; j < e; ++j) {
            const Elem &el = elems[j];
            for (int i = 0; i < n; ++i)
                touched[i] = touched[i] || (el.isLost ? (lost[el.k][el.li].mask & (1u << i)) != 0 : added[i][el.k]);
        }
        bool interesting = false;
        for (int i = 0; i < n; ++i) interesting |= touched[i];
        if (dense)
            for (int i = 0; i < n; ++i) interesting = interesting && touched[i];
        if (!interesting) continue;

        QString header = at;
        for (int i = 0; i < n; ++i) {
            int count = 0, start = -1;
            for (int j = s; j < e; ++j) {
                const Elem &el = elems[j];
                int ln = 0;
                if (el.isLost) {
                    if (lost[el.k][el.li].mask & (1u << i)) ln = lost[el.k][el.li].oldLine[i];
                } else if (!added[i][el.k]) {
                    ln = pline[i][el.k];
                }
                if (ln) {
                    if (start < 0) start = ln;
                    ++count;
                }
            }
            if (start < 0) start = qMax(0, pline[i][elems[s].k] - 1);
            header += QStringLiteral(" -%1,%2").arg(start).arg(count);
        }
        int rcount = 0, rstart = -1;
        for (int j = s; j < e; ++j)
            if (!elems[j].isLost) {
                if (rstart < 0) rstart = elems[j].k + 1;
                ++rcount;
            }
        if (rstart < 0) rstart = elems[s].k;
        header += QStringLiteral(" +%1,%2 ").arg(rstart).arg(rcount) + at;
        f.lines << header;
        for (int j = s; j < e; ++j) {
            const Elem &el = elems[j];
            QString marks(n, QLatin1Char(' '));
            if (el.isLost) {
                const Lost &l = lost[el.k][el.li];
                for (int i = 0; i < n; ++i)
                    if (l.mask & (1u << i)) marks[i] = QLatin1Char('-');
                f.lines << marks + QString::fromUtf8(l.text);
            } else {
                for (int i = 0; i < n; ++i)
                    if (added[i][el.k]) marks[i] = QLatin1Char('+');
                f.lines << marks + QString::fromUtf8(rLines[el.k]);
            }
        }
    }
    return f;
}

DiffResult DiffEngine::diffCommit(const Repository &repo, const Oid &id, const DiffOptions &o) {
    if (id.isWorkdir()) return diffBetween(repo, Oid::indexId(), Oid::workdirId(), o);
    if (id.isIndex()) {
        Oid head = repo.resolveCommit(QStringLiteral("HEAD"));
        DiffResult r = diffBetween(repo, head, Oid::indexId(), o);
        return r;
    }
    CommitPtr c = repo.lookupCommit(id);
    if (!c) return {{}, {}, QStringLiteral("commit %1 not found").arg(id.hex())};
    unsigned np = git_commit_parentcount(c.get());
    if (np <= 1) {
        Oid parent = np ? Oid(git_commit_parent_id(c.get(), 0)) : Oid();
        if (np == 0) {
            TreePtr t = treeForCommit(repo, id);
            StrArray spec(withSubmodulePaths(o.paths, {t.get()}));
            git_diff_options opts = makeOptions(o, spec);
            git_diff *d = nullptr;
            if (git_diff_tree_to_tree(&d, repo.raw(), nullptr, t.get(), &opts) < 0) return {{}, {}, lastGitError()};
            DiffPtr guard(d);
            return fromGitDiff(repo, d, o, {}, 0);
        }
        return diffBetween(repo, parent, id, o);
    }

    // Merge: combined diff over paths that differ from every parent.
    TreePtr rt = treeForCommit(repo, id);
    std::vector<TreePtr> pts;
    QList<QSet<QString>> changed;
    for (unsigned i = 0; i < np; ++i) pts.push_back(treeForCommit(repo, Oid(git_commit_parent_id(c.get(), i))));
    StrArray spec(withSubmodulePaths(o.paths, {rt.get(), pts.front().get()}));
    for (unsigned i = 0; i < np; ++i) {
        git_diff_options opts = makeOptions(o, spec);
        git_diff *d = nullptr;
        QSet<QString> paths;
        if (git_diff_tree_to_tree(&d, repo.raw(), pts.back().get(), rt.get(), &opts) == 0) {
            for (size_t k = 0; k < git_diff_num_deltas(d); ++k) {
                const git_diff_delta *dd = git_diff_get_delta(d, k);
                paths.insert(QString::fromUtf8(dd->new_file.path));
            }
            git_diff_free(d);
        }
        git_error_clear();
        changed.append(paths);
    }
    QSet<QString> common = changed.first();
    for (int i = 1; i < changed.size(); ++i) common.intersect(changed[i]);
    QStringList paths(common.cbegin(), common.cend());
    std::sort(paths.begin(), paths.end());

    DiffResult r;
    for (const QString &path : paths) {
        // A submodule: its commits relative to the first parent (no combined form of those).
        auto linkAt = [&](git_tree *t, Oid *out) {
            QString prefix;
            return gitlinkIn(t, path, &prefix, out) && prefix == path;
        };
        Oid resultLink, firstLink, other;
        bool anyLink = linkAt(rt.get(), &resultLink) | linkAt(pts.front().get(), &firstLink);
        for (size_t i = 1; i < pts.size(); ++i) anyLink |= linkAt(pts[i].get(), &other);
        if (anyLink) {
            if (o.submodules == DiffOptions::SubmoduleSummary) continue;
            SubmoduleChange sc;
            sc.path = path;
            sc.oldId = firstLink;
            sc.newId = resultLink;
            sc.status = resultLink.isNull() ? QLatin1Char('D') : firstLink.isNull() ? QLatin1Char('A') : QLatin1Char('M');
            sc.notes << QStringLiteral("merge: compared with the first parent");
            FileDiff f;
            f.oldPath = f.newPath = path;
            if (resultLink.isNull()) f.newPath.clear();
            f.status = sc.status;
            f.numParents = int(np);
            addSubmodule(repo, std::move(sc), std::move(f), r, o, 0, false);
            continue;
        }
        QList<QByteArray> parentData;
        QStringList parentIds;
        for (const TreePtr &pt : pts) {
            parentData << blobFromTree(repo, pt.get(), path, nullptr);
            parentIds << blobIdAt(pt.get(), path);
        }
        bool inResult = false;
        QByteArray res = blobFromTree(repo, rt.get(), path, &inResult);
        FileDiff f = combinedFileDiff(path, parentData, res, o, true, parentIds, blobIdAt(rt.get(), path));
        if (!inResult) {
            f.status = QLatin1Char('D');
            f.newPath.clear();
        }
        // Drop files whose every hunk turned out uninteresting (keep binary notes).
        bool hasHunk = f.binary;
        for (const QString &l : f.lines) hasHunk |= l.startsWith(QLatin1String("@@"));
        if (hasHunk) r.files.append(std::move(f));
    }
    return r;
}

DiffResult DiffEngine::diffAgainstParent(const Repository &repo, const Oid &id, int parent, const DiffOptions &o) {
    CommitPtr c = repo.lookupCommit(id);
    if (!c || parent < 1 || unsigned(parent) > git_commit_parentcount(c.get()))
        return {{}, {}, QStringLiteral("no such parent")};
    return diffBetween(repo, Oid(git_commit_parent_id(c.get(), unsigned(parent - 1))), id, o);
}

namespace {

DiffResult diffBetweenImpl(const Repository &repo, const Oid &from, const Oid &to, const DiffOptions &o, int depth) {
    // Normalise so that a pseudo id, if any, is on the "to" side; reverse otherwise.
    bool reverse = false;
    Oid a = from, b = to;
    auto rank = [](const Oid &x) { return x.isWorkdir() ? 2 : x.isIndex() ? 1 : 0; };
    if (rank(a) > rank(b)) {
        std::swap(a, b);
        reverse = true;
    }
    Sides sides;
    (reverse ? sides.oldWorkdir : sides.newWorkdir) = b.isWorkdir();
    git_diff *d = nullptr;
    int rc;
    if (b.isWorkdir()) {
        if (a.isWorkdir()) return {};
        IndexPtr idx = repoIndex(repo);
        TreePtr t = a.isIndex() ? TreePtr() : treeForCommit(repo, a);
        StrArray spec(withSubmodulePaths(o.paths, {t.get()}, idx.get()));
        git_diff_options opts = makeOptions(o, spec, reverse);
        // Untracked files only in the plain "index -> work tree" diff (the local changes row).
        if (o.includeUntracked && a.isIndex() && !reverse)
            opts.flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS | GIT_DIFF_SHOW_UNTRACKED_CONTENT;
        if (a.isIndex()) rc = git_diff_index_to_workdir(&d, repo.raw(), idx.get(), &opts);
        else rc = git_diff_tree_to_workdir_with_index(&d, repo.raw(), t.get(), &opts);
    } else if (b.isIndex()) {
        if (a.isIndex()) return {};
        TreePtr t = a.isNull() ? headTree(repo) : treeForCommit(repo, a);
        IndexPtr idx = repoIndex(repo);
        StrArray spec(withSubmodulePaths(o.paths, {t.get()}, idx.get()));
        git_diff_options opts = makeOptions(o, spec, reverse);
        rc = git_diff_tree_to_index(&d, repo.raw(), t.get(), idx.get(), &opts);
    } else {
        TreePtr ta = treeForCommit(repo, a), tb = treeForCommit(repo, b);
        StrArray spec(withSubmodulePaths(o.paths, {ta.get(), tb.get()}));
        git_diff_options opts = makeOptions(o, spec, reverse);
        rc = git_diff_tree_to_tree(&d, repo.raw(), ta.get(), tb.get(), &opts);
    }
    if (rc < 0) return {{}, {}, lastGitError()};
    DiffPtr guard(d);
    return fromGitDiff(repo, d, o, sides, depth);
}

QStringList treeFilesImpl(const Repository &repo, const Oid &id, bool submodules, int depth) {
    QStringList out;
    QList<QPair<int, Oid>> links;   // index into out, recorded commit
    if (id.isPseudo()) {
        IndexPtr idx = repoIndex(repo);
        if (!idx) return out;
        size_t n = git_index_entrycount(idx.get());
        for (size_t i = 0; i < n; ++i) {
            const git_index_entry *e = git_index_get_byindex(idx.get(), i);
            if (GIT_INDEX_ENTRY_STAGE(e) != 0) continue;
            if (e->mode == GIT_FILEMODE_COMMIT) links.append({int(out.size()), Oid(e->id)});
            out << QString::fromUtf8(e->path);
        }
    } else {
        TreePtr t = treeForCommit(repo, id);
        if (!t) return out;
        struct Walk {
            QStringList *out;
            QList<QPair<int, Oid>> *links;
        } walk{&out, &links};
        git_tree_walk(t.get(), GIT_TREEWALK_PRE, [](const char *root, const git_tree_entry *e, void *payload) -> int {
            auto *w = static_cast<Walk *>(payload);
            const git_object_t type = git_tree_entry_type(e);
            if (type == GIT_OBJECT_COMMIT) w->links->append({int(w->out->size()), Oid(git_tree_entry_id(e))});
            if (type == GIT_OBJECT_BLOB || type == GIT_OBJECT_COMMIT)
                w->out->append(QString::fromUtf8(root) + QString::fromUtf8(git_tree_entry_name(e)));
            return 0;
        }, &walk);
    }
    if (!submodules || links.isEmpty() || depth >= kMaxSubmoduleDepth) return out;
    // Submodules whose commit is available are replaced by their files.
    QStringList expanded;
    int next = 0;
    for (const auto &[index, commit] : links) {
        expanded << out.mid(next, index - next);
        next = index + 1;
        const QString path = out[index];
        Repository sub = repo.openSubmodule(path);
        QStringList inner = sub.isValid() && sub.lookupCommit(commit) ? treeFilesImpl(sub, commit, true, depth + 1)
                                                                       : QStringList();
        git_error_clear();
        if (inner.isEmpty()) {
            expanded << path;
            continue;
        }
        for (const QString &p : inner) expanded << path + QLatin1Char('/') + p;
    }
    expanded << out.mid(next);
    return expanded;
}

} // namespace

DiffResult DiffEngine::diffBetween(const Repository &repo, const Oid &from, const Oid &to, const DiffOptions &o) {
    return diffBetweenImpl(repo, from, to, o, 0);
}

const SubmoduleChange *DiffResult::submoduleFor(const QString &path) const {
    const SubmoduleChange *best = nullptr;
    for (const SubmoduleChange &s : submodules) {
        if (path != s.path && !(s.expanded && path.startsWith(s.path + QLatin1Char('/')))) continue;
        if (!best || s.path.size() > best->path.size()) best = &s;
    }
    return best;
}

QStringList DiffEngine::treeFiles(const Repository &repo, const Oid &id, bool submodules) {
    return treeFilesImpl(repo, id, submodules, 0);
}

bool DiffEngine::locateInSubmodule(const Repository &repo, const Oid &id, const QString &path, SubmoduleLocation *out) {
    QString prefix;
    Oid commit;
    if (id.isPseudo()) {
        IndexPtr idx = repoIndex(repo);
        if (!gitlinkIn(idx.get(), path, &prefix, &commit)) return false;
    } else {
        TreePtr t = treeForCommit(repo, id);
        if (!gitlinkIn(t.get(), path, &prefix, &commit)) return false;
    }
    Repository sub = repo.openSubmodule(prefix);
    if (!sub.isValid()) return false;
    const Oid at = id.isWorkdir() ? Oid::workdirId() : commit;
    const QString rest = path.mid(prefix.size() + 1);
    if (!rest.isEmpty() && locateInSubmodule(sub, at, rest, out)) {   // nested
        out->prefix = prefix + QLatin1Char('/') + out->prefix;
        return true;
    }
    *out = {prefix, sub.path(), at, rest};
    return true;
}

QByteArray DiffEngine::fileContents(const Repository &repo, const Oid &id, const QString &path, bool *found) {
    if (found) *found = false;
    if (id.isWorkdir()) {
        QFile f(QDir(repo.workdir()).filePath(path));
        if (!f.open(QIODevice::ReadOnly)) return {};
        if (found) *found = true;
        return f.readAll();
    }
    // A path inside a submodule: read it from the submodule's repository.
    auto inSubmodule = [&](const QString &prefix, const Oid &commit) -> QByteArray {
        if (prefix.size() >= path.size()) return {};
        Repository sub = repo.openSubmodule(prefix);
        if (!sub.isValid()) return {};
        return fileContents(sub, commit, path.mid(prefix.size() + 1), found);
    };
    QString prefix;
    Oid commit;
    if (id.isIndex()) {
        IndexPtr idx = repoIndex(repo);
        if (!idx) return {};
        const git_index_entry *e = git_index_get_bypath(idx.get(), path.toUtf8().constData(), 0);
        if (!e) return gitlinkIn(idx.get(), path, &prefix, &commit) ? inSubmodule(prefix, commit) : QByteArray();
        git_blob *b = nullptr;
        if (git_blob_lookup(&b, repo.raw(), &e->id) < 0) {
            git_error_clear();
            return {};
        }
        BlobPtr bg(b);
        if (found) *found = true;
        return QByteArray(static_cast<const char *>(git_blob_rawcontent(b)), int(git_blob_rawsize(b)));
    }
    TreePtr t = treeForCommit(repo, id);
    bool here = false;
    QByteArray data = blobFromTree(repo, t.get(), path, &here);
    if (found) *found = here;
    if (here || !gitlinkIn(t.get(), path, &prefix, &commit)) return data;
    return inSubmodule(prefix, commit);
}

QList<WordSeg> DiffEngine::wordDiff(const QString &oldText, const QString &newText) {
    auto tokenize = [](const QString &s) {
        QStringList toks;
        int i = 0;
        while (i < s.size()) {
            int j = i;
            QChar c = s[i];
            if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
                while (j < s.size() && (s[j].isLetterOrNumber() || s[j] == QLatin1Char('_'))) ++j;
            } else if (c.isSpace()) {
                while (j < s.size() && s[j].isSpace() && s[j] != QLatin1Char('\n')) ++j;
                if (j == i) ++j;
            } else {
                ++j;
            }
            toks << s.mid(i, j - i);
            i = j;
        }
        return toks;
    };
    const QStringList a = tokenize(oldText), b = tokenize(newText);
    QList<WordSeg> out;
    auto push = [&](WordSeg::Kind k, const QString &t) {
        if (t.isEmpty()) return;
        if (!out.isEmpty() && out.last().kind == k) out.last().text += t;
        else out.append({k, t});
    };
    if (qint64(a.size()) * b.size() > 4'000'000) {
        push(WordSeg::Removed, oldText);
        push(WordSeg::Added, newText);
        return out;
    }
    const int n = int(a.size()), m = int(b.size());
    QList<QList<int>> l(n + 1, QList<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            l[i][j] = a[i] == b[j] ? l[i + 1][j + 1] + 1 : qMax(l[i + 1][j], l[i][j + 1]);
    int i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a[i] == b[j]) {
            push(WordSeg::Same, a[i]);
            ++i, ++j;
        } else if (i < n && (j >= m || l[i + 1][j] >= l[i][j + 1])) {
            push(WordSeg::Removed, a[i++]);
        } else {
            push(WordSeg::Added, b[j++]);
        }
    }
    return out;
}

} // namespace gitq
