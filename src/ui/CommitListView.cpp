#include "ui/CommitListView.h"

#include "ui/GraphDelegate.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QHelpEvent>
#include <QToolTip>

namespace gitq {

CommitListView::CommitListView(QWidget *parent) : QTreeView(parent) {
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionBehavior(SelectRows);
    setSelectionMode(SingleSelection);
    setItemsExpandable(false);
    setTextElideMode(Qt::ElideRight);
    setVerticalScrollMode(ScrollPerPixel);
    header()->setStretchLastSection(false);
    header()->setSectionsMovable(false);
    setMouseTracking(true);
}

void CommitListView::setHoverRow(int row) {
    if (row == m_hoverRow) return;
    auto repaint = [this](int r) {
        if (r < 0 || !model() || r >= model()->rowCount()) return;
        const QRect rect = visualRect(model()->index(r, 0));
        viewport()->update(QRect(0, rect.top(), viewport()->width(), rect.height()));
    };
    repaint(m_hoverRow);
    m_hoverRow = row;
    repaint(m_hoverRow);
}

void CommitListView::mouseMoveEvent(QMouseEvent *e) {
    QTreeView::mouseMoveEvent(e);
    const QModelIndex idx = indexAt(e->pos());
    setHoverRow(idx.isValid() ? idx.row() : -1);
    RefInfo ref;
    const bool onRef = idx.isValid() && idx.column() == 0 && m_delegate && m_delegate->refAt(idx.row(), e->pos(), &ref);
    viewport()->setCursor(onRef ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void CommitListView::leaveEvent(QEvent *e) {
    QTreeView::leaveEvent(e);
    setHoverRow(-1);
}

void CommitListView::setGraphDelegate(GraphDelegate *d) {
    m_delegate = d;
    setItemDelegate(d);
}

void CommitListView::contextMenuEvent(QContextMenuEvent *e) {
    QModelIndex idx = indexAt(e->pos());
    if (!idx.isValid()) return;
    RefInfo ref;
    if (idx.column() == 0 && m_delegate && m_delegate->refAt(idx.row(), e->pos(), &ref) &&
        ref.kind == RefInfo::LocalBranch) {
        emit refContextMenu(idx.row(), ref, e->globalPos());
        return;
    }
    emit rowContextMenu(idx.row(), e->globalPos());
}

void CommitListView::mousePressEvent(QMouseEvent *e) {
    QTreeView::mousePressEvent(e);
    QModelIndex idx = indexAt(e->pos());
    RefInfo ref;
    if (e->button() == Qt::LeftButton && idx.isValid() && idx.column() == 0 && m_delegate &&
        m_delegate->refAt(idx.row(), e->pos(), &ref))
        emit refClicked(idx.row(), ref);
}

bool CommitListView::viewportEvent(QEvent *e) {
    if (e->type() == QEvent::ToolTip && m_delegate) {
        auto *he = static_cast<QHelpEvent *>(e);
        QModelIndex idx = indexAt(he->pos());
        RefInfo ref;
        if (idx.isValid() && idx.column() == 0 && m_delegate->refAt(idx.row(), he->pos(), &ref)) {
            QString tip;
            switch (ref.kind) {
            case RefInfo::LocalBranch: {
                tip = tr("Branch %1").arg(ref.name);
                if (!ref.upstream.isEmpty()) {
                    QStringList parts;
                    auto count = [this](int v) { return v == 1 ? tr("1 commit") : tr("%1 commits").arg(v); };
                    if (ref.ahead > 0) parts << tr("%1 ahead of").arg(count(ref.ahead));
                    if (ref.behind > 0) parts << tr("%1 behind").arg(count(ref.behind));
                    tip += QLatin1Char('\n') + (parts.isEmpty() ? tr("Up to date with %1").arg(ref.upstream)
                                                                 : parts.join(tr(" and ")) + QLatin1Char(' ') + ref.upstream);
                }
                break;
            }
            case RefInfo::RemoteBranch: tip = tr("Remote branch %1").arg(ref.name); break;
            case RefInfo::Tag: tip = tr("Tag %1").arg(ref.name); break;
            case RefInfo::Other: tip = ref.fullName; break;
            }
            QToolTip::showText(he->globalPos(), tip, viewport());
            return true;
        }
    }
    return QTreeView::viewportEvent(e);
}

} // namespace gitq
