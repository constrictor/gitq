// Paints the file list's rows: a status badge, the path (directory dimmed) or, grouped by folder,
// the file name under its folder row, and the added / removed line counts with a small bar. For
// local changes, the row under the mouse gets a Stage / Unstage button.
#pragma once

#include <QStyledItemDelegate>

namespace gitq {

class FileListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    enum RowAction { NoRowAction, StageRow, UnstageRow };
    void setRowAction(RowAction a) { m_action = a; }
    RowAction rowAction() const { return m_action; }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;

signals:
    // The row button of the file at `path` was clicked.
    void rowActionClicked(const QString &path);

protected:
    bool editorEvent(QEvent *e, QAbstractItemModel *model, const QStyleOptionViewItem &opt,
                     const QModelIndex &idx) override;

private:
    // Where the row's button is (null if the row has none).
    QRectF buttonRect(const QStyleOptionViewItem &opt, const QModelIndex &idx) const;
    QString buttonText() const;

    RowAction m_action = NoRowAction;
};

} // namespace gitq
