// Paints the commit list rows: graph, ref labels and headline in the first column, plain text in the
// others. In multi-line mode the first column holds two lines: headline above, author and date below.
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

    void setMultiline(bool on) { m_multiline = on; }
    bool multiline() const { return m_multiline; }

    // Ref label under a point (in viewport coordinates) of the given row, if any.
    bool refAt(int row, const QPoint &pos, RefInfo *out) const;
    // Horizontal centre of the node of `row` in the column rect (for hit tests / scrolling).
    int nodeX(int row, const QRect &cell) const;

private:
    struct Geometry {
        double colWidth;
        double left;
    };
    // Height of one text line within a row of the given rect.
    int lineHeight(const QRect &cell) const;
    Geometry geometry(const QRect &cell) const;
    void paintGraph(QPainter *p, const QRect &cell, double nodeY, int lineH, int row) const;
    int paintRefs(QPainter *p, const QRect &line, int x, int row, const QFont &font) const;

    CommitModel *m_model;
    bool m_multiline = false;
    mutable QHash<int, QList<QPair<QRect, RefInfo>>> m_labelRects;
};

} // namespace gitq
