// Ref labels drawn as rounded "pills" with an icon, shared by the commit list and the commit header.
// A local branch and its upstream at the same commit share one pill; a local branch whose upstream
// is elsewhere shows how many commits it is ahead of / behind it.
#pragma once

#include "git/CommitInfo.h"

#include <QFont>
#include <QList>
#include <QPair>
#include <QRect>
#include <optional>

class QPainter;

namespace gitq {

struct RefPill {
    RefInfo ref;
    std::optional<RefInfo> remote;   // the upstream of `ref`, at the same commit
    bool head = false;               // `ref` is the checked-out branch
};

// Groups the refs of one commit into pills, keeping their order.
QList<RefPill> groupRefPills(const QList<RefInfo> &refs, const QString &headBranch);

class RefPillPainter {
public:
    RefPillPainter(const QFont &font, int height);

    int height() const { return m_h; }
    int width(const RefPill &pill) const;
    // Paints `pill` with its top left corner at (x, y); returns the rectangle of each ref in it.
    QList<QPair<QRect, RefInfo>> paint(QPainter *p, const RefPill &pill, int x, int y) const;

private:
    struct Part {
        QString text;
        QString extra;   // ahead / behind counts
        int width;
    };
    Part localPart(const RefPill &pill) const;
    Part remotePart(const RefPill &pill) const;

    QFont m_font, m_bold, m_small;
    int m_h, m_pad, m_icon, m_gap;
};

} // namespace gitq
