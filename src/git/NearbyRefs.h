// "Branches", "Follows" and "Precedes" information shown in the commit header.
#pragma once

#include "git/Repository.h"

#include <QStringList>
#include <atomic>

namespace gitq {

struct NearbyRefs {
    Oid id;
    QStringList branches;   // local branches containing the commit
    QStringList follows;    // nearest tags that are ancestors
    QStringList precedes;   // nearest tags that are descendants
};

namespace Nearby {
NearbyRefs compute(const Repository &repo, const Oid &id, const RefSet &refs,
                   const std::atomic<bool> *cancel = nullptr);
}

} // namespace gitq
