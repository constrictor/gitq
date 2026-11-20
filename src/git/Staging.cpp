#include "git/Staging.h"

#include "git/GitCli.h"
#include "git/GitPtr.h"
#include "git/Repository.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <unistd.h>

namespace gitq::Ops {

namespace {

GitRun git(const Repository &repo, const QStringList &args, const QByteArray &input = {}) {
    return GitCli::run(repo.workdir(), args, input);
}

OpResult failed(const GitRun &r) { return OpResult::fail(r.errorText()); }

// Paths as NUL-separated literal pathspecs for --pathspec-from-file=- (no limit on their number).
QByteArray pathspecInput(const QStringList &paths) {
    QByteArray out;
    for (const QString &p : paths) out += GitCli::literal(p).toUtf8() + '\0';
    return out;
}

const QStringList kPathspecStdin = {QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul")};

// Rejects paths that could reach outside the work tree.
bool safePath(const QString &p) {
    if (p.isEmpty() || QDir::isAbsolutePath(p)) return false;
    const QStringList parts = p.split(QLatin1Char('/'));
    return !parts.contains(QStringLiteral("..")) && !parts.contains(QStringLiteral(".git"));
}

OpResult checkPaths(const Repository &repo, const QStringList &paths) {
    if (repo.isBare()) return OpResult::fail(QStringLiteral("The repository has no work tree."));
    if (paths.isEmpty()) return OpResult::fail(QStringLiteral("No files given."));
    for (const QString &p : paths)
        if (!safePath(p)) return OpResult::fail(QStringLiteral("Refusing to touch the path \"%1\".").arg(p));
    return {};
}

QString gitqDir(const Repository &repo) {
    const QString d = QDir(repo.path()).filePath(QStringLiteral("gitq"));
    QDir().mkpath(d);
    return d;
}

bool headIsUnborn(const Repository &repo) { return repo.resolveCommit(QStringLiteral("HEAD")).isNull(); }

QString readLink(const QString &file) {
    QByteArray buf(4096, Qt::Uninitialized);
    const QByteArray f = QFile::encodeName(file);
    const ssize_t n = ::readlink(f.constData(), buf.data(), size_t(buf.size()));
    return n < 0 ? QString() : QFile::decodeName(buf.left(int(n)));
}

void logBackup(const Repository &repo, const DiscardBackup &b) {
    QFile log(QDir(gitqDir(repo)).filePath(QStringLiteral("discarded")));
    if (!log.open(QIODevice::Append)) return;
    QString flags;
    if (b.untracked) flags += QLatin1Char('u');
    if (b.executable) flags += QLatin1Char('x');
    if (b.symlink) flags += QLatin1Char('l');
    if (flags.isEmpty()) flags = QStringLiteral("-");
    log.write(QStringLiteral("%1\t%2\t%3\t%4\n")
                  .arg(b.time.toString(Qt::ISODate), b.blob, flags, b.path)
                  .toUtf8());
}

// Saves the work tree content of `path` as a blob. A missing file needs no backup (returns true
// without adding one).
bool backUp(const Repository &repo, const QString &path, bool untracked, QList<DiscardBackup> *out,
            QString *error) {
    const QString abs = QDir(repo.workdir()).filePath(path);
    QFileInfo fi(abs);
    if (!fi.exists() && !fi.isSymLink()) return true;
    DiscardBackup b;
    b.path = path;
    b.untracked = untracked;
    b.time = QDateTime::currentDateTime();
    GitRun r;
    if (fi.isSymLink()) {
        b.symlink = true;
        r = git(repo, {QStringLiteral("hash-object"), QStringLiteral("-w"), QStringLiteral("--stdin")},
                readLink(abs).toUtf8());
    } else {
        b.executable = fi.permissions() & QFile::ExeOwner;
        r = git(repo, {QStringLiteral("hash-object"), QStringLiteral("-w"), QStringLiteral("--no-filters"),
                       QStringLiteral("--"), path});
    }
    if (!r.ok()) {
        *error = QStringLiteral("Could not back up %1: %2").arg(path, r.errorText());
        return false;
    }
    b.blob = QString::fromLatin1(r.out).trimmed();
    logBackup(repo, b);
    out->append(b);
    return true;
}

// Removes the empty directories from `dir` up to (not including) the work tree.
void removeEmptyParents(const QString &workdir, QString dir) {
    const QString root = QDir::cleanPath(workdir);
    while (!dir.isEmpty() && QDir::cleanPath(dir) != root && dir.startsWith(root)) {
        if (!QDir().rmdir(dir)) break;
        dir = QFileInfo(dir).absolutePath();
    }
}

QStringList hunkLines(const QByteArray &hunk) {
    QStringList lines = QString::fromUtf8(hunk).split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines;
}

} // namespace

// ---- Files ----------------------------------------------------------------------------------

OpResult stageFiles(const Repository &repo, const QStringList &paths) {
    if (OpResult c = checkPaths(repo, paths); !c.ok) return c;
    GitRun r = git(repo, QStringList{QStringLiteral("add"), QStringLiteral("-A")} + kPathspecStdin, pathspecInput(paths));
    return r.ok() ? OpResult{} : failed(r);
}

OpResult unstageFiles(const Repository &repo, const QStringList &paths) {
    if (OpResult c = checkPaths(repo, paths); !c.ok) return c;
    QStringList args;
    if (headIsUnborn(repo))
        args = {QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q"),
                QStringLiteral("--ignore-unmatch")};
    else
        args = {QStringLiteral("restore"), QStringLiteral("--staged")};
    GitRun r = git(repo, args + kPathspecStdin, pathspecInput(paths));
    return r.ok() ? OpResult{} : failed(r);
}

OpResult discardFiles(const Repository &repo, const QStringList &paths, QList<DiscardBackup> *backups) {
    if (OpResult c = checkPaths(repo, paths); !c.ok) return c;
    QList<DiscardBackup> made;
    QString error;
    for (const QString &p : paths)
        if (!backUp(repo, p, false, &made, &error)) return OpResult::fail(error);
    if (backups) *backups += made;
    GitRun r = git(repo, QStringList{QStringLiteral("restore"), QStringLiteral("--worktree")} + kPathspecStdin,
                   pathspecInput(paths));
    return r.ok() ? OpResult{} : failed(r);
}

OpResult deleteUntracked(const Repository &repo, const QStringList &paths, QList<DiscardBackup> *backups) {
    if (OpResult c = checkPaths(repo, paths); !c.ok) return c;
    const QString workdir = repo.workdir();
    QList<DiscardBackup> made;
    QString error;
    // Collect the files (a path may be an untracked directory) and back them all up first.
    QStringList files;
    for (const QString &p : paths) {
        const QString abs = QDir(workdir).filePath(p);
        QFileInfo fi(abs);
        if (fi.isDir() && !fi.isSymLink()) {
            QDirIterator it(abs, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) files << QDir(workdir).relativeFilePath(it.next());
        } else {
            files << p;
        }
    }
    for (const QString &f : files)
        if (!backUp(repo, f, true, &made, &error)) return OpResult::fail(error);
    if (backups) *backups += made;
    for (const QString &p : paths) {
        const QString abs = QDir(workdir).filePath(p);
        QFileInfo fi(abs);
        bool ok = fi.isDir() && !fi.isSymLink() ? QDir(abs).removeRecursively() : QFile::remove(abs);
        if (!ok && (fi.exists() || fi.isSymLink())) return OpResult::fail(QStringLiteral("Could not delete %1.").arg(p));
        removeEmptyParents(workdir, fi.absolutePath());
    }
    return {};
}

OpResult restoreDiscarded(const Repository &repo, const DiscardBackup &b) {
    if (OpResult c = checkPaths(repo, {b.path}); !c.ok) return c;
    GitRun r = git(repo, {QStringLiteral("cat-file"), QStringLiteral("blob"), b.blob});
    if (!r.ok()) return failed(r);
    const QString abs = QDir(repo.workdir()).filePath(b.path);
    QFileInfo fi(abs);
    QDir().mkpath(fi.absolutePath());
    if (b.symlink) {
        QFile::remove(abs);
        const QByteArray target = r.out, link = QFile::encodeName(abs);
        if (::symlink(target.constData(), link.constData()) != 0)
            return OpResult::fail(QStringLiteral("Could not create the link %1.").arg(b.path));
        return {};
    }
    if (fi.isSymLink()) QFile::remove(abs);
    QSaveFile f(abs);
    if (!f.open(QIODevice::WriteOnly) || f.write(r.out) != r.out.size() || !f.commit())
        return OpResult::fail(QStringLiteral("Could not write %1.").arg(b.path));
    QFile::Permissions perm = QFile::permissions(abs);
    if (b.executable) perm |= QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther;
    else perm &= ~(QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther);
    QFile::setPermissions(abs, perm);
    return {};
}

bool backupIsCurrent(const Repository &repo, const DiscardBackup &b) {
    const QString abs = QDir(repo.workdir()).filePath(b.path);
    QFileInfo fi(abs);
    if (!fi.exists() && !fi.isSymLink()) return false;
    GitRun r = fi.isSymLink()
                   ? git(repo, {QStringLiteral("hash-object"), QStringLiteral("--stdin")}, readLink(abs).toUtf8())
                   : git(repo, {QStringLiteral("hash-object"), QStringLiteral("--no-filters"), QStringLiteral("--"), b.path});
    return r.ok() && QString::fromLatin1(r.out).trimmed() == b.blob;
}

// ---- Hunks ----------------------------------------------------------------------------------

SplitPatch splitPatch(const QByteArray &patch) {
    SplitPatch s;
    int pos = 0;
    bool inHunks = false;
    while (pos < patch.size()) {
        int nl = patch.indexOf('\n', pos);
        const int end = nl < 0 ? int(patch.size()) : nl + 1;
        const QByteArray line = patch.mid(pos, end - pos);
        if (line.startsWith("@@")) {
            inHunks = true;
            s.hunks.append(line);
        } else if (inHunks) {
            s.hunks.last() += line;
        } else {
            s.header += line;
        }
        pos = end;
    }
    return s;
}

QString hunkRange(const QString &headerLine) {
    const int end = headerLine.indexOf(QStringLiteral("@@"), 2);
    return end < 0 ? headerLine : headerLine.left(end + 2);
}

OpResult applyHunk(const Repository &repo, const HunkRef &hunk, HunkAction action, QList<DiscardBackup> *backups) {
    if (OpResult c = checkPaths(repo, {hunk.path}); !c.ok) return c;
    const int context = qMax(0, hunk.context);
    // The same diff libgit2 showed: Myers without the indent heuristic, no renames, plain prefixes.
    QStringList args = {QStringLiteral("-c"), QStringLiteral("core.quotePath=false"),
                        QStringLiteral("-c"), QStringLiteral("diff.suppressBlankEmpty=false"),
                        QStringLiteral("diff"), QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
                        QStringLiteral("--no-textconv"), QStringLiteral("--no-renames"),
                        QStringLiteral("--diff-algorithm=myers"), QStringLiteral("--no-indent-heuristic"),
                        QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/"),
                        QStringLiteral("--inter-hunk-context=0"), QStringLiteral("-U%1").arg(context)};
    if (action == UnstageHunk) args << QStringLiteral("--cached");
    args << QStringLiteral("--") << GitCli::literal(hunk.path);
    GitRun d = git(repo, args);
    if (!d.ok()) return failed(d);

    const SplitPatch patch = splitPatch(d.out);
    const QString range = hunkRange(hunk.header);
    const QByteArray *found = nullptr;
    for (const QByteArray &h : patch.hunks) {
        QStringList lines = hunkLines(h);
        if (lines.isEmpty() || hunkRange(lines.first()) != range) continue;
        lines.removeFirst();
        if (lines == hunk.body) {
            found = &h;
            break;
        }
    }
    if (!found)
        return OpResult::fail(QStringLiteral("%1 has changed since its diff was shown. The diff has been refreshed; "
                                             "please try again.").arg(hunk.path));

    if (action == DiscardHunk) {
        QList<DiscardBackup> made;
        QString error;
        if (!backUp(repo, hunk.path, false, &made, &error)) return OpResult::fail(error);
        if (backups) *backups += made;
    }
    QStringList apply = {QStringLiteral("apply"), QStringLiteral("--whitespace=nowarn")};
    if (action != DiscardHunk) apply << QStringLiteral("--cached");
    if (action != StageHunk) apply << QStringLiteral("-R");
    if (context == 0) apply << QStringLiteral("--unidiff-zero");
    apply << QStringLiteral("-");
    GitRun r = git(repo, apply, patch.header + *found);
    return r.ok() ? OpResult{} : failed(r);
}

// ---- Commit ---------------------------------------------------------------------------------

QStringList commitArgs(const CommitRequest &req) {
    QStringList args = {QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (req.amend) args << QStringLiteral("--amend");
    if (req.signOff) args << QStringLiteral("--signoff");
    return args;
}

OpResult commitResult(const Repository &repo, bool ok, const QString &output) {
    if (!ok) return OpResult::fail(output);
    OpResult r;
    r.newCommit = repo.resolveCommit(QStringLiteral("HEAD"));
    r.message = output.section(QLatin1Char('\n'), 0, 0).trimmed();
    return r;
}

OpResult commit(const Repository &repo, const CommitRequest &req) {
    if (repo.isBare()) return OpResult::fail(QStringLiteral("The repository has no work tree."));
    GitRun r = git(repo, commitArgs(req), req.message.toUtf8());
    return commitResult(repo, r.ok(), r.ok() ? QString::fromUtf8(r.out) : r.errorText());
}

QString headMessage(const Repository &repo) {
    CommitPtr c = repo.lookupCommit(repo.resolveCommit(QStringLiteral("HEAD")));
    if (!c) return {};
    const char *m = git_commit_message(c.get());
    return m ? QString::fromUtf8(m).trimmed() : QString();
}

QString mergeMessage(const Repository &repo) {
    QFile f(QDir(repo.path()).filePath(QStringLiteral("MERGE_MSG")));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList kept;
    for (const QString &l : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')))
        if (!l.startsWith(QLatin1Char('#'))) kept << l;
    return kept.join(QLatin1Char('\n')).trimmed();
}

StagedSummary stagedSummary(const Repository &repo) {
    StagedSummary s;
    if (repo.isBare()) return s;
    git_index *idx = nullptr;
    if (git_repository_index(&idx, repo.raw()) < 0) {
        git_error_clear();
        return s;
    }
    IndexPtr index(idx);
    git_index_read(idx, 0);
    TreePtr tree;
    if (CommitPtr head = repo.lookupCommit(repo.resolveCommit(QStringLiteral("HEAD")))) {
        git_tree *t = nullptr;
        if (git_commit_tree(&t, head.get()) == 0) tree.reset(t);
    }
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.flags |= GIT_DIFF_IGNORE_SUBMODULES;
    git_diff *d = nullptr;
    if (git_diff_tree_to_index(&d, repo.raw(), tree.get(), idx, &opts) < 0) {
        git_error_clear();
        return s;
    }
    DiffPtr diff(d);
    // Submodule changes count as files too.
    git_diff_options all = GIT_DIFF_OPTIONS_INIT;
    git_diff *d2 = nullptr;
    if (git_diff_tree_to_index(&d2, repo.raw(), tree.get(), idx, &all) == 0) {
        s.files = int(git_diff_num_deltas(d2));
        git_diff_free(d2);
    } else {
        git_error_clear();
    }
    git_diff_stats *stats = nullptr;
    if (git_diff_get_stats(&stats, d) == 0) {
        s.added = int(git_diff_stats_insertions(stats));
        s.removed = int(git_diff_stats_deletions(stats));
        git_diff_stats_free(stats);
    } else {
        git_error_clear();
    }
    return s;
}

QString loadDraft(const Repository &repo) {
    QFile f(QDir(repo.path()).filePath(QStringLiteral("gitq/COMMIT_DRAFT")));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

void saveDraft(const Repository &repo, const QString &text) {
    const QString file = QDir(repo.path()).filePath(QStringLiteral("gitq/COMMIT_DRAFT"));
    if (text.trimmed().isEmpty()) {
        QFile::remove(file);
        return;
    }
    gitqDir(repo);
    QSaveFile f(file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(text.toUtf8());
        f.commit();
    }
}

} // namespace gitq::Ops
