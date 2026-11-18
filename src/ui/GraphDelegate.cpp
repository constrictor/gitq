#include "ui/GraphDelegate.h"

#include "Settings.h"
#include "model/CommitModel.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>

namespace gitq {

GraphDelegate::GraphDelegate(CommitModel *model, QObject *parent) : QStyledItemDelegate(parent), m_model(model) {}

QSize GraphDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    QSize s = QStyledItemDelegate::sizeHint(opt, idx);
    s.setHeight(qMax(s.height(), opt.fontMetrics.height() + 4));
    return s;
}

GraphDelegate::Geometry GraphDelegate::geometry(const QRect &cell) const {
    const Settings &st = Settings::instance();
    double col = cell.height() * 0.75;
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

void GraphDelegate::paintGraph(QPainter *p, const QRect &cell, int row) const {
    const Settings &st = Settings::instance();
    const GraphLayout &graph = m_model->graph();
    if (row >= graph.rowCount()) return;
    const GraphRow &gr = graph.row(row);
    Geometry g = geometry(cell);
    auto x = [&](int c) { return g.left + (c + 0.5) * g.colWidth; };
    auto color = [&](int c) {
        const auto &cols = st.graphColors;
        return cols.isEmpty() ? QColor(Qt::black) : cols.at(c % cols.size());
    };
    const double top = cell.top(), mid = cell.top() + cell.height() / 2.0, bottom = cell.bottom() + 1;
    const double lw = qMax(1.0, cell.height() / 12.0);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    auto line = [&](double x1, double y1, double x2, double y2, int c) {
        p->setPen(QPen(color(c), lw, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p->drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const double ah = qMax(3.0, cell.height() / 4.0), aw = qMax(2.0, g.colWidth / 3.5);
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

    const CommitInfo *c = m_model->commitAt(row);
    double r = qMin(g.colWidth * 0.32, cell.height() * 0.24);
    QColor fill(Qt::blue);
    if (c) {
        if (c->id.isWorkdir()) fill = st.workdirCircleColor;
        else if (c->id.isIndex()) fill = st.indexCircleColor;
        else if (m_model->isHead(c->id)) fill = st.mainHeadCircleColor;
        else if (c->boundary) fill = Qt::white;
    }
    p->setPen(QPen(st.fgColor, 1));
    p->setBrush(fill);
    p->drawEllipse(QPointF(x(gr.nodeCol), mid), r, r);
    p->restore();
}

int GraphDelegate::paintRefs(QPainter *p, const QRect &cell, int x, int row, const QFont &font) const {
    const Settings &st = Settings::instance();
    const CommitInfo *c = m_model->commitAt(row);
    QList<QPair<QRect, RefInfo>> rects;
    if (!c) {
        m_labelRects.remove(row);
        return x;
    }
    const QList<RefInfo> refs = m_model->refsFor(c->id);
    if (refs.isEmpty()) {
        m_labelRects.remove(row);
        return x;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const int h = cell.height() - 4;
    const int y = cell.top() + 2;
    const QString headBranch = m_model->refs().headBranch;
    for (const RefInfo &ref : refs) {
        QFont f = font;
        if (ref.kind == RefInfo::LocalBranch && ref.name == headBranch) f.setBold(true);
        QFontMetrics fm(f);
        p->setFont(f);
        int tw = fm.horizontalAdvance(ref.name);
        int pad = 3;
        int tip = ref.kind == RefInfo::Tag ? h / 2 : 0;
        QRect box(x + tip, y, tw + 2 * pad, h);
        QPainterPath path;
        if (ref.kind == RefInfo::Tag) {
            QPolygonF poly;
            poly << QPointF(x, y + h / 2.0) << QPointF(box.left(), y) << QPointF(box.right(), y)
                 << QPointF(box.right(), y + h) << QPointF(box.left(), y + h);
            path.addPolygon(poly);
            path.closeSubpath();
        } else {
            path.addRect(box);
        }
        QColor bg = ref.kind == RefInfo::LocalBranch ? st.headBgColor
                    : ref.kind == RefInfo::RemoteBranch ? st.remoteBgColor
                    : ref.kind == RefInfo::Tag ? st.tagBgColor
                                               : st.otherRefBgColor;
        p->setPen(QPen(Qt::black, 1));
        p->setBrush(bg);
        p->drawPath(path);
        if (ref.kind == RefInfo::RemoteBranch) {
            // "remote/" prefix in the remote colour, branch part in the head colour, like gitk.
            int slash = ref.name.indexOf(QLatin1Char('/'));
            if (slash > 0) {
                int pw = fm.horizontalAdvance(ref.name.left(slash + 1));
                QRect rest(box.left() + pad + pw, box.top() + 1, box.width() - pad - pw - 1, box.height() - 1);
                p->fillRect(rest, st.headBgColor);
            }
        }
        p->setPen(Qt::black);
        p->drawText(box.adjusted(pad, 0, -pad, 0), Qt::AlignVCenter | Qt::AlignLeft, ref.name);
        rects.append({QRect(x, y, box.right() - x + 1, h), ref});
        x = box.right() + 4;
    }
    p->restore();
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

    QColor bg = st.bgColor;
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

    paintGraph(p, cell, row);
    Geometry g = geometry(cell);
    int w = row < m_model->graph().rowCount() ? m_model->graph().row(row).width : 1;
    int x = int(g.left + w * g.colWidth) + 4;

    // Line from the node to the labels, as gitk does.
    QFont labelFont = font;
    int labelsStart = x;
    x = paintRefs(p, cell, x, row, labelFont);
    if (x != labelsStart) {
        p->save();
        p->setPen(QPen(Qt::black, 1));
        int nx = nodeX(row, cell);
        int y = cell.top() + cell.height() / 2;
        p->drawLine(nx, y, labelsStart, y);
        p->restore();
        x += 4;
    }

    p->setFont(font);
    p->setPen(st.fgColor);
    QRect textRect(x, cell.top(), cell.right() - x, cell.height());
    if (textRect.width() > 0)
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(font).elidedText(opt.text, Qt::ElideRight, textRect.width()));
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
