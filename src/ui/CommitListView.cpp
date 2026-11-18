#include "ui/CommitListView.h"

#include "ui/GraphDelegate.h"

#include <QContextMenuEvent>
#include <QHeaderView>

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

} // namespace gitq
