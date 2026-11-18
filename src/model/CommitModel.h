// Table model for the commit list (headline+graph, author, date).
#pragma once

#include "git/CommitInfo.h"
#include "git/Repository.h"
#include "graph/GraphLayout.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QSet>

namespace gitq {

class CommitModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { SubjectColumn, AuthorColumn, DateColumn, ColumnCount };

    explicit CommitModel(QObject *parent = nullptr);

    void clear();
    void appendCommits(const CommitPtrList &batch);
    // Lays out the remaining graph rows once all commits are loaded.
    void finishLoading();
    void setRefs(const RefSet &refs, bool hideRemotes);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    const CommitInfo *commitAt(int row) const;
    int rowOf(const Oid &id) const { return m_rowOf.value(id, -1); }
    QList<Oid> childrenOf(const Oid &id) const { return m_children.value(id); }
    // Rows whose id starts with the given hex prefix (case-insensitive).
    QList<int> rowsWithPrefix(const QString &hexPrefix) const;
    QList<Oid> allIds() const;

    const GraphLayout &graph() const { return m_graph; }
    const RefSet &refs() const { return m_refs; }
    QList<RefInfo> refsFor(const Oid &id) const;
    bool isHead(const Oid &id) const { return !m_refs.head.isNull() && m_refs.head == id; }

    // Find highlighting (bold rows).
    void setHighlighted(const QSet<Oid> &ids);
    void addHighlighted(const QList<Oid> &ids);
    bool isHighlighted(const Oid &id) const { return m_highlighted.contains(id); }
    const QSet<Oid> &highlighted() const { return m_highlighted; }

    void setMarked(const Oid &id);
    Oid marked() const { return m_marked; }

    void setDateFormat(const QString &fmt);
    QString formatDate(qint64 secs) const;

private:
    void updateGraph(bool final);
    void emitRowChanged(const Oid &id);

    CommitPtrList m_commits;
    QHash<Oid, int> m_rowOf;
    QHash<Oid, QList<Oid>> m_children;
    GraphLayout m_graph;
    RefSet m_refs;
    bool m_hideRemotes = false;
    QSet<Oid> m_highlighted;
    Oid m_marked;
    QString m_dateFormat = QStringLiteral("yyyy-MM-dd HH:mm:ss");
};

} // namespace gitq
