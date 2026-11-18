#include "git/RevArgs.h"

#include "git/Repository.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace gitq {

namespace {

bool takeValue(const QString &arg, const QString &name, QString *value) {
    // matches "--name=value"
    QString prefix = name + QLatin1Char('=');
    if (arg.startsWith(prefix)) {
        *value = arg.mid(prefix.size());
        return true;
    }
    return false;
}

bool hasGlobChars(const QString &s) {
    return s.contains(QLatin1Char('*')) || s.contains(QLatin1Char('?')) || s.contains(QLatin1Char('['));
}

QString globFor(const QString &prefix, const QString &pattern) {
    if (pattern.isEmpty() || pattern == QLatin1String("*")) return prefix + QLatin1Char('*');
    QString p = pattern;
    if (p.startsWith(QLatin1String("refs/"))) p = p.mid(5);
    QString full = prefix + p;
    if (!hasGlobChars(full)) full += QLatin1String("/*");
    return full;
}

struct GlobCollect {
    const Repository *repo;
    QList<Oid> *out;
};

int globCallback(const char *name, void *payload) {
    auto *gc = static_cast<GlobCollect *>(payload);
    git_object *obj = nullptr;
    if (git_revparse_single(&obj, gc->repo->raw(), name) == 0) {
        git_object *peeled = nullptr;
        if (git_object_peel(&peeled, obj, GIT_OBJECT_COMMIT) == 0) {
            gc->out->append(Oid(git_object_id(peeled)));
            git_object_free(peeled);
        }
        git_object_free(obj);
    }
    git_error_clear();
    return 0;
}

void collectGlob(const Repository &repo, const QString &glob, QList<Oid> *out) {
    GlobCollect gc{&repo, out};
    QByteArray g = glob.toUtf8();
    git_reference_foreach_glob(repo.raw(), g.constData(), globCallback, &gc);
    git_error_clear();
}

bool looksLikeRevision(const Repository *repo, const QString &arg) {
    if (!repo) return true;
    QString spec = arg;
    if (spec.startsWith(QLatin1Char('^'))) spec = spec.mid(1);
    for (const QString &sep : {QStringLiteral("..."), QStringLiteral("..")}) {
        int i = spec.indexOf(sep);
        if (i >= 0) {
            QString a = spec.left(i), b = spec.mid(i + sep.size());
            return (a.isEmpty() || !repo->resolveCommit(a).isNull()) &&
                   (b.isEmpty() || !repo->resolveCommit(b).isNull());
        }
    }
    if (spec.endsWith(QLatin1String("^!")) || spec.endsWith(QLatin1String("^@")))
        spec.chop(2);
    return !repo->resolveCommit(spec).isNull();
}

} // namespace

LogOptions RevArgs::parse(const QStringList &args, const Repository *repo, QString *error) {
    LogOptions o;
    bool pathsOnly = false;
    QString v;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (pathsOnly) {
            o.paths << a;
            continue;
        }
        auto next = [&](QString *out) -> bool {
            if (i + 1 >= args.size()) {
                if (error) *error = QStringLiteral("option %1 requires a value").arg(a);
                return false;
            }
            *out = args.at(++i);
            return true;
        };
        if (a == QLatin1String("--")) {
            pathsOnly = true;
        } else if (a == QLatin1String("--all")) {
            o.all = true;
        } else if (a == QLatin1String("--branches")) {
            o.branchGlobs << QStringLiteral("*");
        } else if (takeValue(a, QStringLiteral("--branches"), &v)) {
            o.branchGlobs << v;
        } else if (a == QLatin1String("--tags")) {
            o.tagGlobs << QStringLiteral("*");
        } else if (takeValue(a, QStringLiteral("--tags"), &v)) {
            o.tagGlobs << v;
        } else if (a == QLatin1String("--remotes")) {
            o.remoteGlobs << QStringLiteral("*");
        } else if (takeValue(a, QStringLiteral("--remotes"), &v)) {
            o.remoteGlobs << v;
        } else if (takeValue(a, QStringLiteral("--glob"), &v)) {
            o.refGlobs << v;
        } else if (a == QLatin1String("--date-order") || a == QLatin1String("-d")) {
            o.order = LogOptions::DateOrder;
        } else if (a == QLatin1String("--topo-order")) {
            o.order = LogOptions::TopoOrder;
        } else if (a == QLatin1String("--first-parent")) {
            o.firstParent = true;
        } else if (a == QLatin1String("--no-merges")) {
            o.maxParents = 1;
        } else if (a == QLatin1String("--merges")) {
            o.minParents = 2;
        } else if (takeValue(a, QStringLiteral("--min-parents"), &v)) {
            o.minParents = v.toInt();
        } else if (takeValue(a, QStringLiteral("--max-parents"), &v)) {
            o.maxParents = v.toInt();
        } else if (a == QLatin1String("-n") || a == QLatin1String("--max-count")) {
            if (!next(&v)) return o;
            o.maxCount = v.toInt();
        } else if (takeValue(a, QStringLiteral("--max-count"), &v)) {
            o.maxCount = v.toInt();
        } else if (a.size() > 2 && a.startsWith(QLatin1String("-n")) && a.mid(2).toInt() > 0) {
            o.maxCount = a.mid(2).toInt();
        } else if (a.size() > 1 && a.startsWith(QLatin1Char('-')) && a.mid(1).toInt() > 0) {
            o.maxCount = a.mid(1).toInt();
        } else if (takeValue(a, QStringLiteral("--skip"), &v)) {
            o.skip = v.toInt();
        } else if (takeValue(a, QStringLiteral("--since"), &v) || takeValue(a, QStringLiteral("--after"), &v)) {
            o.since = parseDate(v);
            if (!o.since && error) *error = QStringLiteral("cannot parse date: %1").arg(v);
        } else if (takeValue(a, QStringLiteral("--until"), &v) || takeValue(a, QStringLiteral("--before"), &v)) {
            o.until = parseDate(v);
            if (!o.until && error) *error = QStringLiteral("cannot parse date: %1").arg(v);
        } else if (takeValue(a, QStringLiteral("--author"), &v)) {
            o.authors << v;
        } else if (a == QLatin1String("--author")) {
            if (!next(&v)) return o;
            o.authors << v;
        } else if (takeValue(a, QStringLiteral("--committer"), &v)) {
            o.committers << v;
        } else if (a == QLatin1String("--committer")) {
            if (!next(&v)) return o;
            o.committers << v;
        } else if (takeValue(a, QStringLiteral("--grep"), &v)) {
            o.greps << v;
        } else if (a == QLatin1String("--grep")) {
            if (!next(&v)) return o;
            o.greps << v;
        } else if (a == QLatin1String("--all-match")) {
            o.allMatch = true;
        } else if (a == QLatin1String("-i") || a == QLatin1String("--regexp-ignore-case")) {
            o.regexIgnoreCase = true;
        } else if (a == QLatin1String("-F") || a == QLatin1String("--fixed-strings")) {
            o.fixedStrings = true;
        } else if (a == QLatin1String("-E") || a == QLatin1String("--extended-regexp")) {
            o.fixedStrings = false;
        } else if (a == QLatin1String("--full-history")) {
            o.fullHistory = true;
        } else if (a == QLatin1String("--boundary")) {
            o.boundary = true;
        } else if (a == QLatin1String("--not")) {
            o.revisions << a;   // handled during resolution
        } else if (a.startsWith(QLatin1String("--"))) {
            if (error) *error = QStringLiteral("unsupported option: %1").arg(a);
        } else if (looksLikeRevision(repo, a)) {
            o.revisions << a;
        } else if (repo && !repo->workdir().isEmpty() &&
                   QFileInfo::exists(QDir(repo->workdir()).filePath(a))) {
            o.paths << a;
        } else if (repo && QFileInfo::exists(a)) {
            o.paths << a;
        } else {
            if (error) *error = QStringLiteral("ambiguous argument '%1': unknown revision or path").arg(a);
            o.revisions << a;
        }
    }
    return o;
}

ResolvedRevs RevArgs::resolve(const Repository &repo, const LogOptions &o, QString *error) {
    ResolvedRevs r;
    bool positiveGiven = false;
    bool negate = false;

    auto add = [&](const Oid &id, bool hide) {
        if (id.isNull()) return;
        (hide ? r.exclude : r.include).append(id);
        if (!hide) positiveGiven = true;
    };
    auto resolveOne = [&](const QString &spec) -> Oid {
        QString err;
        Oid id = repo.resolveCommit(spec, &err);
        if (id.isNull() && error && error->isEmpty()) *error = err;
        return id;
    };

    for (const QString &raw : o.revisions) {
        if (raw == QLatin1String("--not")) {
            negate = !negate;
            continue;
        }
        QString spec = raw;
        bool hide = negate;
        if (spec.startsWith(QLatin1Char('^'))) {
            hide = !hide;
            spec = spec.mid(1);
        }
        int sym = spec.indexOf(QLatin1String("..."));
        int dots = sym < 0 ? spec.indexOf(QLatin1String("..")) : -1;
        if (sym >= 0) {
            QString a = spec.left(sym), b = spec.mid(sym + 3);
            Oid ia = resolveOne(a.isEmpty() ? QStringLiteral("HEAD") : a);
            Oid ib = resolveOne(b.isEmpty() ? QStringLiteral("HEAD") : b);
            add(ia, false);
            add(ib, false);
            git_oidarray bases{};
            git_oid both[2] = {ia.oid, ib.oid};
            if (!ia.isNull() && !ib.isNull() && git_merge_bases_many(&bases, repo.raw(), 2, both) == 0) {
                for (size_t k = 0; k < bases.count; ++k) add(Oid(bases.ids[k]), true);
                git_oidarray_dispose(&bases);
            }
            git_error_clear();
        } else if (dots >= 0) {
            QString a = spec.left(dots), b = spec.mid(dots + 2);
            add(resolveOne(a.isEmpty() ? QStringLiteral("HEAD") : a), !hide);
            add(resolveOne(b.isEmpty() ? QStringLiteral("HEAD") : b), hide);
        } else if (spec.endsWith(QLatin1String("^!"))) {
            Oid id = resolveOne(spec.left(spec.size() - 2));
            add(id, hide);
            if (auto c = repo.lookupCommit(id))
                for (unsigned k = 0; k < git_commit_parentcount(c.get()); ++k)
                    add(Oid(git_commit_parent_id(c.get(), k)), !hide);
        } else if (spec.endsWith(QLatin1String("^@"))) {
            Oid id = resolveOne(spec.left(spec.size() - 2));
            if (auto c = repo.lookupCommit(id))
                for (unsigned k = 0; k < git_commit_parentcount(c.get()); ++k)
                    add(Oid(git_commit_parent_id(c.get(), k)), hide);
        } else {
            add(resolveOne(spec), hide);
        }
    }

    QList<Oid> globbed;
    if (o.all) {
        collectGlob(repo, QStringLiteral("refs/*"), &globbed);
        Oid head = repo.resolveCommit(QStringLiteral("HEAD"));
        if (!head.isNull()) globbed << head;
    }
    for (const QString &g : o.branchGlobs) collectGlob(repo, globFor(QStringLiteral("refs/heads/"), g), &globbed);
    for (const QString &g : o.tagGlobs) collectGlob(repo, globFor(QStringLiteral("refs/tags/"), g), &globbed);
    for (const QString &g : o.remoteGlobs) collectGlob(repo, globFor(QStringLiteral("refs/remotes/"), g), &globbed);
    for (const QString &g : o.refGlobs) collectGlob(repo, globFor(QStringLiteral("refs/"), g), &globbed);
    for (const Oid &id : globbed) add(id, false);

    bool anyGlobOption = o.all || !o.branchGlobs.isEmpty() || !o.tagGlobs.isEmpty() ||
                         !o.remoteGlobs.isEmpty() || !o.refGlobs.isEmpty();
    if (!positiveGiven && !anyGlobOption) {
        Oid head = repo.resolveCommit(QStringLiteral("HEAD"));
        add(head, false);
    }
    return r;
}

qint64 RevArgs::parseDate(const QString &textIn, qint64 now) {
    QString text = textIn.trimmed().toLower();
    if (now == 0) now = QDateTime::currentSecsSinceEpoch();
    if (text.isEmpty()) return 0;
    if (text == QLatin1String("now")) return now;
    if (text == QLatin1String("yesterday")) return now - 86400;
    if (text.startsWith(QLatin1Char('@'))) return text.mid(1).toLongLong();
    bool ok = false;
    qint64 n = text.toLongLong(&ok);
    if (ok && n > 100000000) return n;

    static const QRegularExpression rel(
        QStringLiteral("^(\\d+)[\\s.]*(second|minute|hour|day|week|month|year)s?([\\s.]+ago)?$"));
    auto m = rel.match(text);
    if (m.hasMatch()) {
        qint64 count = m.captured(1).toLongLong();
        const QString unit = m.captured(2);
        QDateTime t = QDateTime::fromSecsSinceEpoch(now);
        if (unit == QLatin1String("second")) t = t.addSecs(-count);
        else if (unit == QLatin1String("minute")) t = t.addSecs(-count * 60);
        else if (unit == QLatin1String("hour")) t = t.addSecs(-count * 3600);
        else if (unit == QLatin1String("day")) t = t.addDays(-count);
        else if (unit == QLatin1String("week")) t = t.addDays(-count * 7);
        else if (unit == QLatin1String("month")) t = t.addMonths(int(-count));
        else if (unit == QLatin1String("year")) t = t.addYears(int(-count));
        return t.toSecsSinceEpoch();
    }

    const QString original = textIn.trimmed();
    for (const char *fmt : {"yyyy-MM-dd HH:mm:ss", "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd HH:mm", "yyyy-MM-dd",
                            "yyyy.MM.dd", "yyyy/MM/dd", "dd.MM.yyyy", "MM/dd/yyyy",
                            "MMMM d, yyyy HH:mm:ss", "MMMM d, yyyy", "MMM d, yyyy HH:mm:ss", "MMM d, yyyy"}) {
        QDateTime dt = QDateTime::fromString(original, QString::fromLatin1(fmt));
        if (dt.isValid()) return dt.toSecsSinceEpoch();
    }
    QDateTime dt = QDateTime::fromString(original, Qt::ISODate);
    if (dt.isValid()) return dt.toSecsSinceEpoch();
    dt = QDateTime::fromString(original, Qt::RFC2822Date);
    if (dt.isValid()) return dt.toSecsSinceEpoch();
    return 0;
}

QStringList RevArgs::toArgs(const LogOptions &o) {
    QStringList a;
    if (o.all) a << QStringLiteral("--all");
    for (const QString &g : o.branchGlobs) a << (g == QLatin1String("*") ? QStringLiteral("--branches") : QStringLiteral("--branches=") + g);
    for (const QString &g : o.tagGlobs) a << (g == QLatin1String("*") ? QStringLiteral("--tags") : QStringLiteral("--tags=") + g);
    for (const QString &g : o.remoteGlobs) a << (g == QLatin1String("*") ? QStringLiteral("--remotes") : QStringLiteral("--remotes=") + g);
    for (const QString &g : o.refGlobs) a << QStringLiteral("--glob=") + g;
    if (o.order == LogOptions::DateOrder) a << QStringLiteral("--date-order");
    if (o.firstParent) a << QStringLiteral("--first-parent");
    if (o.minParents > 0) a << QStringLiteral("--min-parents=%1").arg(o.minParents);
    if (o.maxParents >= 0) a << QStringLiteral("--max-parents=%1").arg(o.maxParents);
    if (o.maxCount >= 0) a << QStringLiteral("--max-count=%1").arg(o.maxCount);
    if (o.skip > 0) a << QStringLiteral("--skip=%1").arg(o.skip);
    if (o.since) a << QStringLiteral("--since=@%1").arg(o.since);
    if (o.until) a << QStringLiteral("--until=@%1").arg(o.until);
    for (const QString &s : o.authors) a << QStringLiteral("--author=") + s;
    for (const QString &s : o.committers) a << QStringLiteral("--committer=") + s;
    for (const QString &s : o.greps) a << QStringLiteral("--grep=") + s;
    if (o.allMatch) a << QStringLiteral("--all-match");
    if (o.regexIgnoreCase) a << QStringLiteral("-i");
    if (o.fixedStrings) a << QStringLiteral("-F");
    if (o.fullHistory) a << QStringLiteral("--full-history");
    a << o.revisions;
    if (!o.paths.isEmpty()) a << QStringLiteral("--") << o.paths;
    return a;
}

} // namespace gitq
