#include "graph/GraphLayout.h"

namespace gitq {

void GraphLayout::clear() {
    m_commits.clear();
    m_rowOf.clear();
    m_rows.clear();
    m_lanes.clear();
    m_reentry.clear();
    m_awaiting.clear();
    m_nextColor = 0;
    m_maxWidth = 0;
}

void GraphLayout::addCommit(const Oid &id, const QList<Oid> &parents) {
    const int row = int(m_commits.size());
    m_commits.append({id, parents});
    m_rowOf.insert(id, row);
    auto it = m_awaiting.find(id);
    if (it != m_awaiting.end()) {
        qint16 color = it.value();
        m_awaiting.erase(it);
        scheduleReentry(id, row, color);
    }
}

void GraphLayout::scheduleReentry(const Oid &parent, int parentRow, qint16 color) {
    int at = qMax(parentRow - m_arrowLen, int(m_rows.size()));
    if (at >= parentRow) return;   // too close: the lane is simply re-created at the parent
    m_reentry.insert(at, {parent, color});
}

int GraphLayout::process(bool final) {
    const int start = int(m_rows.size());
    const int total = int(m_commits.size());
    while (m_rows.size() < total) {
        const int r = int(m_rows.size());
        if (!final) {
            if (r >= total - m_arrowLen) break;
            bool ready = true;
            for (const Oid &p : m_commits[r].parents)
                if (!m_rowOf.contains(p) && total - r <= m_minGap) {
                    ready = false;
                    break;
                }
            if (!ready) break;
        }
        layoutRow(r, final);
    }
    return int(m_rows.size()) - start;
}

qint16 GraphLayout::pickColor(const QList<Lane> &lanes, int col, int avoid) {
    auto colorAt = [&](int i) { return (i >= 0 && i < lanes.size()) ? lanes.at(i).color : qint16(-1); };
    for (int attempt = 0; attempt < m_numColors; ++attempt) {
        qint16 c = qint16((m_nextColor + attempt) % m_numColors);
        if (c == avoid || c == colorAt(col - 1) || c == colorAt(col + 1)) continue;
        m_nextColor = (c + 1) % m_numColors;
        return c;
    }
    qint16 c = qint16(m_nextColor);
    m_nextColor = (m_nextColor + 1) % m_numColors;
    return c;
}

void GraphLayout::layoutRow(int r, bool final) {
    const Oid id = m_commits[r].id;
    const QList<Oid> parents = m_commits[r].parents;
    auto findLane = [](const QList<Lane> &lanes, const Oid &x) {
        for (int i = 0; i < lanes.size(); ++i)
            if (!lanes[i].empty() && lanes[i].id == x) return i;
        return -1;
    };

    // Lanes of elided edges re-entering at this row.
    for (auto it = m_reentry.find(r); it != m_reentry.end() && it.key() == r; it = m_reentry.erase(it)) {
        if (findLane(m_lanes, it->first) >= 0) continue;
        Lane l{it->first, it->second, -1, true};
        int slot = -1;
        for (int i = 0; i < m_lanes.size(); ++i)
            if (m_lanes[i].empty()) {
                slot = i;
                break;
            }
        if (slot < 0) m_lanes.append(l);
        else m_lanes[slot] = l;
    }

    GraphRow row;
    const QList<Lane> before = m_lanes;

    // Node column: the first lane expecting this commit, else the first free slot.
    int col = findLane(before, id);
    QList<Lane> mid = before;
    for (Lane &l : mid) l.arrowUp = false;
    qint16 nodeColor;
    if (col < 0) {
        for (int i = 0; i < mid.size(); ++i)
            if (mid[i].empty()) {
                col = i;
                break;
            }
        if (col < 0) {
            col = int(mid.size());
            mid.append(Lane());
        }
        nodeColor = pickColor(mid, col, -1);
    } else {
        nodeColor = before[col].color;
    }

    // Top half: lanes coming from above.
    for (int i = 0; i < before.size(); ++i) {
        const Lane &l = before[i];
        if (l.empty()) continue;
        quint8 flags = l.arrowUp ? GraphSeg::ArrowUp : GraphSeg::None;
        if (l.id == id) {
            row.top.append({qint16(i), qint16(col), l.color, flags});
            mid[i] = Lane();
        } else {
            row.top.append({qint16(i), qint16(i), l.color, flags});
        }
    }

    // Place parents in the lanes below the node.
    struct NodeEdge { int slot; qint16 color; };
    QList<NodeEdge> nodeEdges;
    const int total = int(m_commits.size());
    for (int p = 0; p < parents.size(); ++p) {
        const Oid &pid = parents[p];
        int existing = findLane(mid, pid);
        if (existing >= 0) {
            mid[existing].elideAt = -1;   // now also a short edge: keep the lane
            nodeEdges.append({existing, p == 0 ? nodeColor : mid[existing].color});
            continue;
        }
        int slot = -1;
        if (p == 0 && mid[col].empty()) {
            slot = col;
        } else {
            for (int i = col + 1; i < mid.size(); ++i)
                if (mid[i].empty()) {
                    slot = i;
                    break;
                }
            if (slot < 0) {
                slot = int(mid.size());
                mid.append(Lane());
            }
        }
        qint16 color = p == 0 ? nodeColor : pickColor(mid, slot, nodeColor);
        Lane lane{pid, color, -1, false};
        const int pr = m_rowOf.value(pid, -1);
        const bool far = pr >= 0 ? pr - r > m_minGap : (final || total - r > m_minGap);
        if (far && m_minGap > 0) {
            lane.elideAt = r + m_arrowLen;
            if (pr < 0 && final) lane.elideAt = qMin(lane.elideAt, total - 1);   // list ends: cut early
            if (pr >= 0) scheduleReentry(pid, pr, color);
            else if (!final) m_awaiting.insert(pid, color);
        }
        mid[slot] = lane;
        nodeEdges.append({slot, color});
    }

    // Lanes whose elision point is reached end here with a down arrow.
    QList<bool> cut(mid.size(), false);
    for (int i = 0; i < mid.size(); ++i)
        if (!mid[i].empty() && mid[i].elideAt >= 0 && mid[i].elideAt <= r) cut[i] = true;

    // Compact: drop empty / cut slots; lanes to the right shift left diagonally.
    QList<int> newIndex(mid.size(), -1);
    QList<Lane> after;
    for (int i = 0; i < mid.size(); ++i) {
        if (mid[i].empty() || cut[i]) continue;
        newIndex[i] = int(after.size());
        after.append(mid[i]);
    }

    // Bottom half: pass-through lanes and edges from the node to its parents.
    QList<bool> fromNode(mid.size(), false);
    for (const NodeEdge &e : nodeEdges) {
        if (cut[e.slot]) row.bottom.append({qint16(col), qint16(e.slot), e.color, GraphSeg::ArrowDown});
        else row.bottom.append({qint16(col), qint16(newIndex[e.slot]), e.color, GraphSeg::None});
        fromNode[e.slot] = true;
    }
    for (int i = 0; i < mid.size(); ++i) {
        if (mid[i].empty()) continue;
        bool passThrough = i < before.size() && !before[i].empty() && before[i].id == mid[i].id && before[i].id != id;
        if (!passThrough && fromNode[i]) continue;
        if (cut[i]) row.bottom.append({qint16(i), qint16(i), mid[i].color, GraphSeg::ArrowDown});
        else row.bottom.append({qint16(i), qint16(newIndex[i]), mid[i].color, GraphSeg::None});
    }

    row.nodeCol = qint16(col);
    row.nodeColor = nodeColor;
    int width = col + 1;
    for (const auto &s : row.top) width = qMax(width, qMax(int(s.from), int(s.to)) + 1);
    for (const auto &s : row.bottom) width = qMax(width, qMax(int(s.from), int(s.to)) + 1);
    row.width = qint16(width);
    m_maxWidth = qMax(m_maxWidth, width);
    m_rows.append(row);
    m_lanes = after;
}

} // namespace gitq
