// Incremental lane layout for the commit graph.
//
// Each row records where lines enter from the row above (top half: row top -> node centre line)
// and where they leave towards the row below (bottom half: centre line -> row bottom), in
// column units. Colours are indices into a palette.
//
// Like gitk, edges spanning more than `minGap` rows are elided: the line is drawn for
// `arrowLen` rows below the child, ends in a down arrow, frees its column, and reappears
// with an up arrow `arrowLen` rows above the parent. Because that needs to know where the
// parent is, a row is laid out only once its parents are loaded or known to be far away.
#pragma once

#include "git/Oid.h"

#include <QHash>
#include <QList>
#include <QMap>

namespace gitq {

struct GraphSeg {
    enum Flag : quint8 { None = 0, ArrowUp = 1, ArrowDown = 2 };
    qint16 from = 0;
    qint16 to = 0;
    qint16 color = 0;
    quint8 flags = None;   // ArrowUp: at the top end of a top seg; ArrowDown: at the bottom end of a bottom seg
    bool operator==(const GraphSeg &) const = default;
};

struct GraphRow {
    qint16 nodeCol = 0;
    qint16 nodeColor = 0;
    qint16 width = 0;          // number of columns occupied in this row
    QList<GraphSeg> top;       // y: row top -> centre
    QList<GraphSeg> bottom;    // y: centre -> row bottom
};

class GraphLayout {
public:
    explicit GraphLayout(int numColors = 7) : m_numColors(numColors) {}

    void clear();
    // Queue the next commit in display order; call process() to lay out queued rows.
    void addCommit(const Oid &id, const QList<Oid> &parents);
    // Lay out as many queued rows as possible. With `final`, all remaining rows are laid out
    // (parents that never arrived are treated as far away). Returns the number of new rows.
    int process(bool final);

    int rowCount() const { return int(m_rows.size()); }      // rows laid out so far
    int commitCount() const { return int(m_commits.size()); } // rows queued
    const GraphRow &row(int i) const { return m_rows.at(i); }
    int maxWidth() const { return m_maxWidth; }

    void setElision(int minGap, int arrowLen) {
        m_minGap = minGap;
        m_arrowLen = arrowLen;
    }

private:
    struct Lane {
        Oid id;
        qint16 color = -1;
        int elideAt = -1;      // row at whose bottom the lane is cut with a down arrow
        bool arrowUp = false;  // lane re-enters at the top of the current row
        bool empty() const { return color < 0; }
    };
    struct Commit {
        Oid id;
        QList<Oid> parents;
    };
    qint16 pickColor(const QList<Lane> &lanes, int col, int avoid);
    void layoutRow(int r, bool final);
    void scheduleReentry(const Oid &parent, int parentRow, qint16 color);

    QList<Commit> m_commits;
    QHash<Oid, int> m_rowOf;
    QList<GraphRow> m_rows;
    QList<Lane> m_lanes;                          // lanes at the bottom of the last laid-out row
    QMultiMap<int, QPair<Oid, qint16>> m_reentry; // row -> (parent, colour) lanes to re-insert
    QHash<Oid, qint16> m_awaiting;                // elided parents not loaded yet -> colour
    int m_numColors;
    int m_nextColor = 0;
    int m_maxWidth = 0;
    int m_minGap = 100;
    int m_arrowLen = 5;
};

} // namespace gitq
