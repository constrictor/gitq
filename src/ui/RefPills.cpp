#include "ui/RefPills.h"

#include "Settings.h"
#include "ui/Glyphs.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace gitq {

QList<RefPill> groupRefPills(const QList<RefInfo> &refs, const QString &headBranch) {
    QList<RefPill> pills;
    QList<bool> used(refs.size(), false);
    for (int i = 0; i < refs.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        RefPill pill;
        pill.ref = refs[i];
        if (refs[i].kind == RefInfo::LocalBranch) {
            pill.head = refs[i].name == headBranch;
            if (!refs[i].upstream.isEmpty())
                for (int j = 0; j < refs.size(); ++j)
                    if (!used[j] && refs[j].kind == RefInfo::RemoteBranch && refs[j].name == refs[i].upstream) {
                        pill.remote = refs[j];
                        used[j] = true;
                        break;
                    }
        }
        pills.append(pill);
    }
    return pills;
}

namespace {

Glyph glyphFor(RefInfo::Kind k) {
    switch (k) {
    case RefInfo::LocalBranch: return Glyph::Branch;
    case RefInfo::RemoteBranch: return Glyph::Cloud;
    case RefInfo::Tag: return Glyph::Tag;
    case RefInfo::Other: break;
    }
    return Glyph::Ref;
}

QColor bgFor(RefInfo::Kind k) {
    const Settings &st = Settings::instance();
    switch (k) {
    case RefInfo::LocalBranch: return st.headBgColor;
    case RefInfo::RemoteBranch: return st.remoteBgColor;
    case RefInfo::Tag: return st.tagBgColor;
    case RefInfo::Other: break;
    }
    return st.otherRefBgColor;
}

// Text on a label: the foreground colour unless it would not stand out from the label.
QColor textOn(const QColor &bg) {
    const QColor fg = Settings::instance().fgColor;
    if (qAbs(fg.lightness() - bg.lightness()) >= 90) return fg;
    return bg.lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white);
}

QColor borderFor(const QColor &bg, bool strong) { return Settings::mix(bg, textOn(bg), strong ? 0.6 : 0.3); }

} // namespace

RefPillPainter::RefPillPainter(const QFont &font, int height) : m_font(font), m_bold(font), m_small(font), m_h(height) {
    m_bold.setBold(true);
    m_small.setBold(false);
    if (m_small.pointSizeF() > 0) m_small.setPointSizeF(m_small.pointSizeF() * 0.85);
    else m_small.setPixelSize(qMax(6, int(m_small.pixelSize() * 0.85)));
    m_pad = qMax(3, int(height * 0.35));
    m_icon = qMax(6, int(height * 0.62));
    m_gap = qMax(2, int(height * 0.2));
}

RefPillPainter::Part RefPillPainter::localPart(const RefPill &pill) const {
    Part part;
    part.text = pill.ref.name;
    if (!pill.remote && !pill.ref.upstream.isEmpty()) {
        QStringList counts;
        if (pill.ref.ahead > 0) counts << QStringLiteral("↑%1").arg(pill.ref.ahead);
        if (pill.ref.behind > 0) counts << QStringLiteral("↓%1").arg(pill.ref.behind);
        part.extra = counts.join(QLatin1Char(' '));
    }
    const QFontMetrics fm(pill.head ? m_bold : m_font);
    part.width = m_pad + m_icon + m_gap + fm.horizontalAdvance(part.text) + m_pad;
    if (!part.extra.isEmpty()) part.width += m_gap + QFontMetrics(m_small).horizontalAdvance(part.extra);
    return part;
}

RefPillPainter::Part RefPillPainter::remotePart(const RefPill &pill) const {
    Part part;
    if (!pill.remote) return {QString(), QString(), 0};
    // "origin" for origin/master next to master; the full name if the branch names differ.
    const QString name = pill.remote->name;
    const int slash = int(name.indexOf(QLatin1Char('/')));
    part.text = slash > 0 && name.mid(slash + 1) == pill.ref.name ? name.left(slash) : name;
    part.width = m_gap + m_icon + m_gap + QFontMetrics(m_font).horizontalAdvance(part.text) + m_pad;
    return part;
}

int RefPillPainter::width(const RefPill &pill) const { return localPart(pill).width + remotePart(pill).width; }

QList<QPair<QRect, RefInfo>> RefPillPainter::paint(QPainter *p, const RefPill &pill, int x, int y) const {
    const Part local = localPart(pill), remote = remotePart(pill);
    const QRectF box(x + 0.5, y + 0.5, local.width + remote.width - 1, m_h - 1);
    const double radius = m_h * 0.32;
    const QColor bg = bgFor(pill.ref.kind);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath shape;
    shape.addRoundedRect(box, radius, radius);
    p->fillPath(shape, bg);
    if (pill.remote) {
        p->save();
        p->setClipRect(QRectF(x + local.width, y, remote.width, m_h));
        p->fillPath(shape, bgFor(RefInfo::RemoteBranch));
        p->restore();
        p->setPen(QPen(borderFor(bg, false), 1));
        p->drawLine(QPointF(x + local.width + 0.5, y + 2), QPointF(x + local.width + 0.5, y + m_h - 2));
    }
    p->setPen(QPen(borderFor(bg, pill.head), pill.head ? 1.6 : 1.0));
    p->setBrush(Qt::NoBrush);
    p->drawPath(shape);

    auto drawPart = [&](const QColor &partBg, Glyph glyph, int left, const QString &text, const QFont &font,
                        const QString &extra) {
        const QColor fg = textOn(partBg);
        const QRectF icon(left, y + (m_h - m_icon) / 2.0, m_icon, m_icon);
        drawGlyph(p, glyph, icon, Settings::mix(fg, partBg, 0.25));
        int tx = left + m_icon + m_gap;
        p->setFont(font);
        p->setPen(fg);
        const int tw = QFontMetrics(font).horizontalAdvance(text);
        p->drawText(QRect(tx, y, tw + 1, m_h), Qt::AlignVCenter | Qt::AlignLeft, text);
        if (!extra.isEmpty()) {
            tx += tw + m_gap;
            p->setFont(m_small);
            p->setPen(Settings::mix(fg, partBg, 0.25));
            p->drawText(QRect(tx, y, QFontMetrics(m_small).horizontalAdvance(extra) + 1, m_h),
                        Qt::AlignVCenter | Qt::AlignLeft, extra);
        }
    };
    drawPart(bg, glyphFor(pill.ref.kind), x + m_pad, local.text, pill.head ? m_bold : m_font, local.extra);
    QList<QPair<QRect, RefInfo>> rects{{QRect(x, y, local.width, m_h), pill.ref}};
    if (pill.remote) {
        drawPart(bgFor(RefInfo::RemoteBranch), Glyph::Cloud, x + local.width + m_gap, remote.text, m_font, QString());
        rects.append({QRect(x + local.width, y, remote.width, m_h), *pill.remote});
    }
    p->restore();
    return rects;
}

} // namespace gitq
