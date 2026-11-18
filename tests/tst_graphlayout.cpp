#include "graph/GraphLayout.h"

#include <QTest>

using namespace gitq;

namespace {
Oid id(int n) {
    git_oid o{};
    o.id[0] = 0xff;
    o.id[1] = quint8(n);
    return Oid(o);
}

// Lines leaving a row at the bottom must enter the next row at the top at the same columns.
void verifyContinuity(const GraphLayout &g) {
    for (int r = 0; r + 1 < g.rowCount(); ++r) {
        QSet<int> out, in;
        for (const auto &s : g.row(r).bottom) out.insert(s.to);
        for (const auto &s : g.row(r + 1).top) in.insert(s.from);
        QCOMPARE(in, out);
    }
}
// Like verifyContinuity, but lanes cut with a down arrow / starting with an up arrow are exempt.
void verifyContinuityAllowingArrows(const GraphLayout &g) {
    for (int r = 0; r + 1 < g.rowCount(); ++r) {
        QSet<int> out, in;
        for (const auto &s : g.row(r).bottom)
            if (!(s.flags & GraphSeg::ArrowDown)) out.insert(s.to);
        for (const auto &s : g.row(r + 1).top)
            if (!(s.flags & GraphSeg::ArrowUp)) in.insert(s.from);
        QCOMPARE(in, out);
    }
}
} // namespace

class TestGraphLayout : public QObject {
    Q_OBJECT
private slots:
    void linear() {
        GraphLayout g;
        g.addCommit(id(1), {id(2)});
        g.addCommit(id(2), {id(3)});
        g.addCommit(id(3), {});
        g.process(true);
        for (int i = 0; i < 3; ++i) QCOMPARE(g.row(i).nodeCol, qint16(0));
        QCOMPARE(g.maxWidth(), 1);
        QVERIFY(g.row(2).bottom.isEmpty());
        verifyContinuity(g);
    }

    void branchAndMerge() {
        // 1 = merge(2, 3); 2 -> 4; 3 -> 4; 4 root
        GraphLayout g;
        g.addCommit(id(1), {id(2), id(3)});
        g.addCommit(id(2), {id(4)});
        g.addCommit(id(3), {id(4)});
        g.addCommit(id(4), {});
        g.process(true);
        QCOMPARE(g.row(0).nodeCol, qint16(0));
        QCOMPARE(g.row(0).bottom.size(), 2);
        QCOMPARE(g.row(1).nodeCol, qint16(0));
        QCOMPARE(g.row(2).nodeCol, qint16(1));
        QCOMPARE(g.row(3).nodeCol, qint16(0));
        QCOMPARE(g.maxWidth(), 2);
        verifyContinuity(g);
        // first parent keeps the merge's colour
        QCOMPARE(g.row(1).nodeColor, g.row(0).nodeColor);
        QVERIFY(g.row(2).nodeColor != g.row(0).nodeColor);
    }

    void twoHeads() {
        // heads 1 and 2 both have parent 3
        GraphLayout g;
        g.addCommit(id(1), {id(3)});
        g.addCommit(id(2), {id(3)});
        g.addCommit(id(3), {});
        g.process(true);
        QCOMPARE(g.row(1).nodeCol, qint16(1));
        QCOMPARE(g.row(2).nodeCol, qint16(0));
        // the second head joins the existing lane of its parent right away
        QCOMPARE(g.row(1).bottom.size(), 2);   // node edge 1->0 plus pass-through 0->0
        QCOMPARE(g.row(1).bottom[0].from, qint16(1));
        QCOMPARE(g.row(1).bottom[0].to, qint16(0));
        QCOMPARE(g.row(2).top.size(), 1);
        verifyContinuity(g);
    }

    void octopusAndCompaction() {
        GraphLayout g;
        g.addCommit(id(1), {id(2), id(3), id(4)});
        g.addCommit(id(3), {id(5)});
        g.addCommit(id(2), {id(5)});
        g.addCommit(id(4), {id(5)});
        g.addCommit(id(5), {});
        g.process(true);
        QCOMPARE(g.maxWidth(), 3);
        QCOMPARE(g.row(4).nodeCol, qint16(0));
        verifyContinuity(g);
    }

    void longEdgesAreElided() {
        // row 0: merge of 1 (long first-parent chain) and 900 (far below)
        GraphLayout g;
        g.addCommit(id(0), {id(1), id(200)});
        for (int i = 1; i < 199; ++i) g.addCommit(id(i), {id(i + 1)});
        g.addCommit(id(199), {id(200)});
        g.addCommit(id(200), {});
        QCOMPARE(g.process(false), 201 - 5);   // waits for the last rows
        g.process(true);
        QCOMPARE(g.rowCount(), 201);
        verifyContinuityAllowingArrows(g);
        // The side lane is cut after 5 rows and re-enters 5 rows above its parent.
        bool down = false, up = false;
        for (const auto &s : g.row(5).bottom) down |= (s.flags & GraphSeg::ArrowDown) != 0;
        for (const auto &s : g.row(195).top) up |= (s.flags & GraphSeg::ArrowUp) != 0;
        QVERIFY(down);
        QVERIFY(up);
        QCOMPARE(g.row(100).width, qint16(1));
        QCOMPARE(g.row(200).top.size(), 1);   // the re-entered lane joined at row 199
    }

    void missingParentEndsWithArrow() {
        GraphLayout g;
        g.addCommit(id(1), {id(2)});
        g.addCommit(id(2), {id(3)});   // 3 never arrives (e.g. --max-count)
        g.process(true);
        QCOMPARE(g.rowCount(), 2);
        QVERIFY(g.row(1).bottom.first().flags & GraphSeg::ArrowDown);
    }
};

QTEST_GUILESS_MAIN(TestGraphLayout)
#include "tst_graphlayout.moc"
