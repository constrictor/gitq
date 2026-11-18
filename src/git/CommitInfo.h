#pragma once

#include "git/Oid.h"

#include <QList>
#include <QSharedPointer>
#include <QString>

namespace gitq {

struct Person {
    QString name;
    QString email;
    qint64 time = 0;    // seconds since epoch
    int offset = 0;     // minutes east of UTC

    QString nameEmail() const { return email.isEmpty() ? name : name + QStringLiteral(" <") + email + QLatin1Char('>'); }
};

struct CommitInfo {
    Oid id;
    QList<Oid> parents;       // possibly rewritten by history simplification
    Person author;
    Person committer;
    QString subject;          // first line (headline)
    QByteArray rawMessage;    // full message, UTF-8 (kept compact; use message())

    QString message() const { return QString::fromUtf8(rawMessage); }
    bool boundary = false;    // shown as a boundary commit (excluded by a range)
};

using CommitPtrList = QList<QSharedPointer<CommitInfo>>;

struct RefInfo {
    enum Kind { LocalBranch, RemoteBranch, Tag, Other };
    Kind kind = Other;
    QString fullName;   // refs/heads/master
    QString name;       // master, origin/master, v1.0
    Oid target;         // direct target (tag object for annotated tags)
    Oid commit;         // peeled commit id
};

} // namespace gitq
