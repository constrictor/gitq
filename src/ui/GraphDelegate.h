// Paints the graph, ref labels and headline in the first column of the commit list.
#pragma once

#include "git/CommitInfo.h"

#include <QHash>
#include <QStyledItemDelegate>

namespace gitq {

class CommitModel;

class GraphDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GraphDelegate(CommitModel *model, QObject *parent = nullptr);

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;

    // Ref label under a point (in viewport coordinates) of the given row, if any.
    bool refAt(int row, const QPoint &pos, RefInfo *out) const;
    // Horizontal centre of the node of `row` in the column rect (for hit tests / scrolling).
    int nodeX(int row, const QRect &cell) const;

private:
    struct Geometry {
        double colWidth;
        double left;
    };
    Geometry geometry(const QRect &cell) const;
    void paintGraph(QPainter *p, const QRect &cell, int row) const;
    int paintRefs(QPainter *p, const QRect &cell, int x, int row, const QFont &font) const;

    CommitModel *m_model;
    mutable QHash<int, QList<QPair<QRect, RefInfo>>> m_labelRects;
};

} // namespace gitq
