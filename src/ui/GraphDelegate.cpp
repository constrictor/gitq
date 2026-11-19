#include "ui/GraphDelegate.h"

#include "Settings.h"
#include "model/CommitModel.h"
#include "ui/RefPills.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>

namespace gitq {

GraphDelegate::GraphDelegate(CommitModel *model, QObject *parent) : QStyledItemDelegate(parent), m_model(model) {}

namespace {
constexpr int kMultiPad = 4;   // vertical padding of a multi-line row, split above and below
}

QSize GraphDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    QSize s = QStyledItemDelegate::sizeHint(opt, idx);
    s.setHeight(qMax(s.height(), opt.fontMetrics.height() + 4));
    if (m_multiline) s.setHeight(2 * s.height() + kMultiPad);
    return s;
}

int GraphDelegate::lineHeight(const QRect &cell) const {
    return m_multiline ? (cell.height() - kMultiPad) / 2 : cell.height();
}

GraphDelegate::Geometry GraphDelegate::geometry(const QRect &cell) const {
    const Settings &st = Settings::instance();
    double col = lineHeight(cell) * 0.75;
    // Shrink columns only when the widest row would not fit in maxGraphPct of the column
    // (rows wider than maxGraphWidth lanes don't force further shrinking).
    int maxCols = qBound(1, m_model->graph().maxWidth(), qMax(1, st.maxGraphWidth));
    double limit = cell.width() * qBound(10, st.maxGraphPct, 100) / 100.0;
    if (maxCols * col > limit) col = qMax(4.0, limit / maxCols);
    return {col, cell.left() + 4.0};
}

int GraphDelegate::nodeX(int row, const QRect &cell) const {
    if (row < 0 || row >= m_model->graph().rowCount()) return cell.left();
    Geometry g = geometry(cell);
    return int(g.left + (m_model->graph().row(row).nodeCol + 0.5) * g.colWidth);
}

void GraphDelegate::paintGraph(QPainter *p, const QRect &cell, double nodeY, int lineH, int row, const QColor &bg) const {
    const Settings &st = Settings::instance();
    const GraphLayout &graph = m_model->graph();
    if (row >= graph.rowCount()) return;
    const GraphRow &gr = graph.row(row);
    Geometry g = geometry(cell);
    auto x = [&](int c) { return g.left + (c + 0.5) * g.colWidth; };
    auto color = [&](int c) {
        const auto &cols = st.graphColors;
        return cols.isEmpty() ? st.fgColor : cols.at(c % cols.size());
    };
    const double top = cell.top(), mid = nodeY, bottom = cell.bottom() + 1;
    const double lw = qBound(1.5, lineH / 9.0, 3.0);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    // A lane that moves to another column is drawn as an S-curve, vertical at both ends, so that it
    // joins the halves of the rows above and below smoothly.
    auto line = [&](double x1, double y1, double x2, double y2, int c) {
        p->setPen(QPen(color(c), lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        if (qFuzzyCompare(x1, x2)) {
            p->drawLine(QPointF(x1, y1), QPointF(x2, y2));
            return;
        }
        const double ym = (y1 + y2) / 2;
        QPainterPath path(QPointF(x1, y1));
        path.cubicTo(QPointF(x1, ym), QPointF(x2, ym), QPointF(x2, y2));
        p->drawPath(path);
    };
    const double ah = qMax(3.0, lineH / 4.0), aw = qMax(2.0, g.colWidth / 3.5);
    auto arrow = [&](double ax, double tipY, double dir, int c) {   // dir -1: up, +1: down
        QPolygonF tri;
        tri << QPointF(ax, tipY) << QPointF(ax - aw, tipY - dir * ah) << QPointF(ax + aw, tipY - dir * ah);
        p->setPen(Qt::NoPen);
        p->setBrush(color(c));
        p->drawPolygon(tri);
    };
    for (const GraphSeg &s : gr.top) {
        line(x(s.from), top, x(s.to), mid, s.color);
        if (s.flags & GraphSeg::ArrowUp) arrow(x(s.from), top, -1, s.color);
    }
    for (const GraphSeg &s : gr.bottom) {
        if (s.flags & GraphSeg::ArrowDown) {
            line(x(s.from), mid, x(s.to), bottom - ah, s.color);
            arrow(x(s.to), bottom, 1, s.color);
        } else {
            line(x(s.from), mid, x(s.to), bottom, s.color);
        }
    }

    // The node: filled with its lane colour; merges hollow; HEAD with a ring around it; local
    // changes and boundary commits as outlines.
    const CommitInfo *c = m_model->commitAt(row);
    const QPointF centre(x(gr.nodeCol), mid);
    const double r = qMin(g.colWidth * 0.34, lineH * 0.26);
    const QColor lane = color(gr.nodeColor);
    QPen pen(lane, lw);
    QBrush fill(lane);
    if (c) {
        if (c->id.isWorkdir() || c->id.isIndex()) {
            pen = QPen(c->id.isWorkdir() ? st.workdirCircleColor : st.indexCircleColor, lw, Qt::DotLine);
            fill = bg;
        } else if (c->boundary) {
            pen = QPen(Settings::mix(st.fgColor, bg, 0.5), lw, Qt::DashLine);
            fill = bg;
        } else if (c->parents.size() > 1) {
            fill = bg;
        }
    }
    p->setPen(pen);
    p->setBrush(fill);
    p->drawEllipse(centre, r, r);
    if (c && m_model->isHead(c->id)) {
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(st.fgColor, qMax(1.0, lw * 0.75)));
        const double ring = r + lw * 1.5;
        p->drawEllipse(centre, ring, ring);
    }
    p->restore();
}

int GraphDelegate::paintRefs(QPainter *p, const QRect &line, int x, int row, const QFont &font) const {
    const CommitInfo *c = m_model->commitAt(row);
    const QList<RefInfo> refs = c ? m_model->refsFor(c->id) : QList<RefInfo>();
    if (refs.isEmpty()) {
        m_labelRects.remove(row);
        return x;
    }
    const RefPillPainter pills(font, line.height() - 4);
    QList<QPair<QRect, RefInfo>> rects;
    for (const RefPill &pill : groupRefPills(refs, m_model->refs().headBranch)) {
        rects += pills.paint(p, pill, x, line.top() + 2);
        x += pills.width(pill) + 4;
    }
    m_labelRects.insert(row, rects);
    return x;
}

void GraphDelegate::paint(QPainter *p, const QStyleOptionViewItem &optIn, const QModelIndex &idx) const {
    const Settings &st = Settings::instance();
    QStyleOptionViewItem opt = optIn;
    initStyleOption(&opt, idx);
    const QRect cell = opt.rect;
    const int row = idx.row();
    const CommitInfo *c = m_model->commitAt(row);

    // Subtle stripes on odd rows; the mark and the selection take precedence.
    QColor bg = row % 2 ? Settings::mix(st.bgColor, st.fgColor, 0.05) : st.bgColor;
    if (c && !m_model->marked().isNull() && c->id == m_model->marked()) bg = st.markBgColor;
    if (opt.state & QStyle::State_Selected) bg = st.selectBgColor;
    p->fillRect(cell, bg);

    QFont font = opt.font;
    if (idx.column() != CommitModel::SubjectColumn) {
        p->setFont(font);
        p->setPen(st.fgColor);
        p->drawText(cell.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, cell.width() - 6));
        return;
    }

    // First line: graph node, ref labels and headline. In multi-line mode the graph lanes still
    // span the whole row.
    const int lineH = lineHeight(cell);
    const QRect line1(cell.left(), cell.top() + (m_multiline ? kMultiPad / 2 : 0), cell.width(), lineH);
    const double nodeY = line1.top() + lineH / 2.0;
    paintGraph(p, cell, nodeY, lineH, row, bg);
    Geometry g = geometry(cell);
    int w = row < m_model->graph().rowCount() ? m_model->graph().row(row).width : 1;
    const int graphRight = int(g.left + w * g.colWidth) + 4;
    int x = graphRight;

    // Line from the node to the labels, as gitk does.
    QFont labelFont = font;
    int labelsStart = x;
    x = paintRefs(p, line1, x, row, labelFont);
    if (x != labelsStart) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const auto &cols = st.graphColors;
        const int lane = row < m_model->graph().rowCount() ? m_model->graph().row(row).nodeColor : 0;
        p->setPen(QPen(Settings::mix(cols.isEmpty() ? st.fgColor : cols.at(lane % cols.size()), bg, 0.3), 1.5));
        p->drawLine(QPointF(nodeX(row, cell), nodeY), QPointF(labelsStart, nodeY));
        p->restore();
        x += 4;
    }

    p->setFont(font);
    p->setPen(st.fgColor);
    QRect textRect(x, line1.top(), cell.right() - x, lineH);
    if (textRect.width() > 0)
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(font).elidedText(opt.text, Qt::ElideRight, textRect.width()));

    if (!m_multiline || !c) return;

    // Second line: author on the left, date on the right, dimmed and never bold.
    QFont plain = font;
    plain.setBold(false);
    const QFontMetrics fm(plain);
    p->setFont(plain);
    p->setPen(Settings::mix(st.fgColor, bg, 0.4));
    QRect line2(graphRight, line1.bottom() + 1, cell.right() - 4 - graphRight, lineH);
    if (line2.width() <= 0) return;
    const QString date = c->id.isPseudo() ? QString() : m_model->formatDate(c->author.time);
    int authorWidth = line2.width();
    if (!date.isEmpty()) {
        const int dw = fm.horizontalAdvance(date);
        if (dw <= line2.width()) {
            p->drawText(line2, Qt::AlignVCenter | Qt::AlignRight, date);
            authorWidth = line2.width() - dw - fm.horizontalAdvance(QLatin1Char(' ')) * 3;
        }
    }
    if (authorWidth > 0)
        p->drawText(QRect(line2.left(), line2.top(), authorWidth, lineH), Qt::AlignVCenter | Qt::AlignLeft,
                    fm.elidedText(c->author.name, Qt::ElideRight, authorWidth));
}

bool GraphDelegate::refAt(int row, const QPoint &pos, RefInfo *out) const {
    auto it = m_labelRects.constFind(row);
    if (it == m_labelRects.cend()) return false;
    for (const auto &pr : *it)
        if (pr.first.contains(pos)) {
            if (out) *out = pr.second;
            return true;
        }
    return false;
}

} // namespace gitq
