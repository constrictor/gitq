// Paints the file list's rows: a status badge, the path (directory dimmed) or, grouped by folder,
// the file name under its folder row, and the added / removed line counts with a small bar.
#pragma once

#include <QStyledItemDelegate>

namespace gitq {

class FileListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
};

} // namespace gitq
