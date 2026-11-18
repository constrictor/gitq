#include "git/DiffEngine.h"

#include "git/GitPtr.h"
#include "git/Repository.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <algorithm>
#include <vector>

namespace gitq {

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
    default: return QLatin1Char('M');
    }
}

DiffResult fromGitDiff(git_diff *diff, const DiffOptions &o) {
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
        if (d->status == GIT_DELTA_ADDED) f.oldPath.clear();
        f.status = statusChar(d->status);
        f.binary = d->flags & GIT_DIFF_FLAG_BINARY;
        BufGuard buf;
        if (p && git_patch_to_buf(&buf.buf, p) == 0) f.lines = splitText(buf.bytes());
        git_error_clear();
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
    if (!c) return {{}, QStringLiteral("commit %1 not found").arg(id.hex())};
    unsigned np = git_commit_parentcount(c.get());
    if (np <= 1) {
        Oid parent = np ? Oid(git_commit_parent_id(c.get(), 0)) : Oid();
        if (np == 0) {
            StrArray spec(o.paths);
            git_diff_options opts = makeOptions(o, spec);
            TreePtr t = treeForCommit(repo, id);
            git_diff *d = nullptr;
            if (git_diff_tree_to_tree(&d, repo.raw(), nullptr, t.get(), &opts) < 0) return {{}, lastGitError()};
            DiffPtr guard(d);
            return fromGitDiff(d, o);
        }
        return diffBetween(repo, parent, id, o);
    }

    // Merge: combined diff over paths that differ from every parent.
    TreePtr rt = treeForCommit(repo, id);
    std::vector<TreePtr> pts;
    QList<QSet<QString>> changed;
    StrArray spec(o.paths);
    for (unsigned i = 0; i < np; ++i) {
        pts.push_back(treeForCommit(repo, Oid(git_commit_parent_id(c.get(), i))));
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
        return {{}, QStringLiteral("no such parent")};
    return diffBetween(repo, Oid(git_commit_parent_id(c.get(), unsigned(parent - 1))), id, o);
}

DiffResult DiffEngine::diffBetween(const Repository &repo, const Oid &from, const Oid &to, const DiffOptions &o) {
    // Normalise so that a pseudo id, if any, is on the "to" side; reverse otherwise.
    bool reverse = false;
    Oid a = from, b = to;
    auto rank = [](const Oid &x) { return x.isWorkdir() ? 2 : x.isIndex() ? 1 : 0; };
    if (rank(a) > rank(b)) {
        std::swap(a, b);
        reverse = true;
    }
    StrArray spec(o.paths);
    git_diff_options opts = makeOptions(o, spec, reverse);
    git_diff *d = nullptr;
    int rc;
    if (b.isWorkdir()) {
        if (a.isIndex()) {
            IndexPtr idx = repoIndex(repo);
            rc = git_diff_index_to_workdir(&d, repo.raw(), idx.get(), &opts);
        } else if (a.isWorkdir()) {
            return {};
        } else {
            TreePtr t = treeForCommit(repo, a);
            rc = git_diff_tree_to_workdir_with_index(&d, repo.raw(), t.get(), &opts);
        }
    } else if (b.isIndex()) {
        if (a.isIndex()) return {};
        TreePtr t = a.isNull() ? headTree(repo) : treeForCommit(repo, a);
        IndexPtr idx = repoIndex(repo);
        rc = git_diff_tree_to_index(&d, repo.raw(), t.get(), idx.get(), &opts);
    } else {
        TreePtr ta = treeForCommit(repo, a), tb = treeForCommit(repo, b);
        rc = git_diff_tree_to_tree(&d, repo.raw(), ta.get(), tb.get(), &opts);
    }
    if (rc < 0) return {{}, lastGitError()};
    DiffPtr guard(d);
    return fromGitDiff(d, o);
}

QStringList DiffEngine::treeFiles(const Repository &repo, const Oid &id) {
    QStringList out;
    if (id.isPseudo()) {
        IndexPtr idx = repoIndex(repo);
        if (!idx) return out;
        size_t n = git_index_entrycount(idx.get());
        for (size_t i = 0; i < n; ++i) {
            const git_index_entry *e = git_index_get_byindex(idx.get(), i);
            if (GIT_INDEX_ENTRY_STAGE(e) == 0) out << QString::fromUtf8(e->path);
        }
        return out;
    }
    TreePtr t = treeForCommit(repo, id);
    if (!t) return out;
    git_tree_walk(t.get(), GIT_TREEWALK_PRE, [](const char *root, const git_tree_entry *e, void *payload) -> int {
        if (git_tree_entry_type(e) == GIT_OBJECT_BLOB || git_tree_entry_type(e) == GIT_OBJECT_COMMIT)
            static_cast<QStringList *>(payload)->append(QString::fromUtf8(root) + QString::fromUtf8(git_tree_entry_name(e)));
        return 0;
    }, &out);
    return out;
}

QByteArray DiffEngine::fileContents(const Repository &repo, const Oid &id, const QString &path, bool *found) {
    if (found) *found = false;
    if (id.isWorkdir()) {
        QFile f(QDir(repo.workdir()).filePath(path));
        if (!f.open(QIODevice::ReadOnly)) return {};
        if (found) *found = true;
        return f.readAll();
    }
    if (id.isIndex()) {
        IndexPtr idx = repoIndex(repo);
        if (!idx) return {};
        const git_index_entry *e = git_index_get_bypath(idx.get(), path.toUtf8().constData(), 0);
        if (!e) return {};
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
    return blobFromTree(repo, t.get(), path, found);
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
