// Line-by-line blame of a file at a commit.
#pragma once

#include "git/CommitInfo.h"

#include <QList>
#include <QString>

namespace gitq {

class Repository;

struct BlameLine {
    Oid commit;
    QString origPath;
    int origLine = 0;       // 1-based line number in `commit`
    Person author;
    QString summary;
    QString text;
};

struct BlameResult {
    QList<BlameLine> lines;
    QString error;
};

namespace Blame {
// Blame `path` as of commit `at` (null: HEAD).
BlameResult blameFile(const Repository &repo, const QString &path, const Oid &at);
}

} // namespace gitq
