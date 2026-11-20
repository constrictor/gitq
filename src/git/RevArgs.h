// Parsing of gitk / git rev-list style command line arguments.
#pragma once

#include "git/Oid.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace gitq {

class Repository;

struct LogOptions {
    enum Order { TopoOrder, DateOrder, NoOrder };

    // Revision arguments, verbatim (e.g. "master", "^v1.0", "a..b", "a...b", "x^!").
    QStringList revisions;
    bool all = false;
    QStringList branchGlobs;   // --branches[=pattern] ("*" when given without pattern)
    QStringList tagGlobs;
    QStringList remoteGlobs;
    QStringList refGlobs;      // --glob=pattern (relative to refs/)

    Order order = TopoOrder;
    bool firstParent = false;
    int minParents = 0;
    int maxParents = -1;       // -1: unlimited
    int maxCount = -1;
    int skip = 0;
    qint64 since = 0;          // 0: unset
    qint64 until = 0;
    QStringList authors;       // regexes
    QStringList committers;
    QStringList greps;
    bool allMatch = false;
    bool regexIgnoreCase = false;
    bool fixedStrings = false;
    bool fullHistory = false;  // disable history simplification for path limiting
    bool boundary = true;      // show boundary commits of ranges (gitk always does)

    QStringList paths;

    // Not a rev-list option: prepend pseudo commits for local changes when HEAD is in the view.
    bool showLocalChanges = false;
    bool showUntracked = false;   // untracked files make the local changes row appear

    bool operator==(const LogOptions &) const = default;
};

struct ResolvedRevs {
    QList<Oid> include;
    QList<Oid> exclude;
};

class RevArgs {
public:
    // Parse arguments. When `repo` is given, a non-option argument that is not a
    // revision but an existing path is treated as a path (as git does).
    static LogOptions parse(const QStringList &args, const Repository *repo, QString *error);

    // Resolve the revision part of `opts` into tips to walk from/hide.
    static ResolvedRevs resolve(const Repository &repo, const LogOptions &opts, QString *error);

    // Parse git approxidate-like strings ("2 weeks ago", "2024-01-31", "@1700000000").
    // Returns 0 on failure.
    static qint64 parseDate(const QString &text, qint64 now = 0);

    // Convert options back to an argument list (used for saving views).
    static QStringList toArgs(const LogOptions &opts);
};

} // namespace gitq
