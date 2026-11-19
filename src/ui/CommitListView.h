// The commit list (gitk's top pane).
#pragma once

#include "git/CommitInfo.h"

#include <QTreeView>

namespace gitq {

class GraphDelegate;

class CommitListView : public QTreeView {
    Q_OBJECT
public:
    explicit CommitListView(QWidget *parent = nullptr);
    void setGraphDelegate(GraphDelegate *d);
    void keyboardSearch(const QString &) override {}   // letters are gitk key bindings
    // Row under the mouse pointer, -1 if none.
    int hoverRow() const { return m_hoverRow; }

signals:
    void rowContextMenu(int row, const QPoint &globalPos);
    void refContextMenu(int row, const gitq::RefInfo &ref, const QPoint &globalPos);
    void refClicked(int row, const gitq::RefInfo &ref);

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    bool viewportEvent(QEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void setHoverRow(int row);

    GraphDelegate *m_delegate = nullptr;
    int m_hoverRow = -1;
};

} // namespace gitq
