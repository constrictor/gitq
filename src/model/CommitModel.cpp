#include "model/CommitModel.h"

#include <QDateTime>
#include <QFont>

namespace gitq {

CommitModel::CommitModel(QObject *parent) : QAbstractTableModel(parent) {}

void CommitModel::clear() {
    beginResetModel();
    m_commits.clear();
    m_rowOf.clear();
    m_children.clear();
    m_graph.clear();
    m_highlighted.clear();
    endResetModel();
}

void CommitModel::appendCommits(const CommitPtrList &batch) {
    if (batch.isEmpty()) return;
    QList<QSharedPointer<CommitInfo>> fresh;
    fresh.reserve(batch.size());
    for (const auto &c : batch)
        if (!m_rowOf.contains(c->id)) fresh.append(c);
    if (fresh.isEmpty()) return;
    const int first = int(m_commits.size());
    beginInsertRows({}, first, first + int(fresh.size()) - 1);
    for (const auto &c : fresh) {
        m_rowOf.insert(c->id, int(m_commits.size()));
        m_commits.append(c);
        for (const Oid &p : c->parents) m_children[p].append(c->id);
        m_graph.addCommit(c->id, c->parents);
    }
    endInsertRows();
    updateGraph(false);
}

void CommitModel::updateGraph(bool final) {
    const int before = m_graph.rowCount();
    if (m_graph.process(final) > 0)
        emit dataChanged(index(before, SubjectColumn), index(m_graph.rowCount() - 1, SubjectColumn));
}

void CommitModel::finishLoading() { updateGraph(true); }

void CommitModel::setRefs(const RefSet &refs, bool hideRemotes) {
    m_refs = refs;
    m_hideRemotes = hideRemotes;
    if (!m_commits.isEmpty()) emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
}

QList<RefInfo> CommitModel::refsFor(const Oid &id) const {
    QList<RefInfo> list = m_refs.refsFor(id);
    if (m_hideRemotes)
        list.removeIf([](const RefInfo &r) { return r.kind == RefInfo::RemoteBranch; });
    std::stable_sort(list.begin(), list.end(), [](const RefInfo &a, const RefInfo &b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.name < b.name;
    });
    return list;
}

int CommitModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_commits.size());
}

int CommitModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

const CommitInfo *CommitModel::commitAt(int row) const {
    return row >= 0 && row < m_commits.size() ? m_commits.at(row).data() : nullptr;
}

QString CommitModel::formatDate(qint64 secs) const {
    return QDateTime::fromSecsSinceEpoch(secs).toString(m_dateFormat);
}

QString CommitModel::relativeTime(qint64 secs, qint64 now) {
    // No translations are loaded, so plurals are spelled out rather than left to "%n(s)".
    auto n = [](qint64 v, const QString &one, const QString &many) { return v == 1 ? one : many.arg(v); };
    const qint64 d = now - secs;
    if (d < -60) return QString();
    if (d < 45) return tr("just now");
    const qint64 min = (d + 30) / 60, hours = (d + 1800) / 3600, days = (d + 43200) / 86400;
    if (min < 60) return n(min, tr("1 minute ago"), tr("%1 minutes ago"));
    if (hours < 24) return n(hours, tr("1 hour ago"), tr("%1 hours ago"));
    if (days == 1) return tr("yesterday");
    if (days < 14) return n(days, tr("1 day ago"), tr("%1 days ago"));
    if (days < 60) return n((days + 3) / 7, tr("1 week ago"), tr("%1 weeks ago"));
    if (days < 365) return n((days + 15) / 30, tr("1 month ago"), tr("%1 months ago"));
    const qint64 years = days / 365, months = (days % 365 + 15) / 30;
    if (years < 5 && months > 0 && months < 12)
        return n(years, tr("1 year"), tr("%1 years")) + QStringLiteral(", ") +
               n(months, tr("1 month ago"), tr("%1 months ago"));
    return n((days + 182) / 365, tr("1 year ago"), tr("%1 years ago"));
}

QString CommitModel::displayDate(qint64 secs) const {
    if (m_relativeDates) {
        const QString rel = relativeTime(secs, QDateTime::currentSecsSinceEpoch());
        if (!rel.isEmpty()) return rel;
    }
    return formatDate(secs);
}

void CommitModel::setRelativeDates(bool on) {
    if (m_relativeDates == on) return;
    m_relativeDates = on;
    if (!m_commits.isEmpty()) emit dataChanged(index(0, 0), index(rowCount() - 1, DateColumn));
}

void CommitModel::setDateFormat(const QString &fmt) {
    m_dateFormat = fmt;
    if (!m_commits.isEmpty()) emit dataChanged(index(0, DateColumn), index(rowCount() - 1, DateColumn));
}

QVariant CommitModel::data(const QModelIndex &idx, int role) const {
    const CommitInfo *c = commitAt(idx.row());
    if (!c) return {};
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        switch (idx.column()) {
        case SubjectColumn:
            if (role == Qt::ToolTipRole && !c->id.isPseudo())
                return QStringLiteral("%1\n%2\n%3").arg(c->subject, c->author.nameEmail(), formatDate(c->author.time));
            return c->subject;
        case AuthorColumn:
            if (role == Qt::ToolTipRole) return c->author.nameEmail();
            return c->author.name;
        case DateColumn:
            if (c->id.isPseudo()) return QString();
            return role == Qt::ToolTipRole ? formatDate(c->author.time) : displayDate(c->author.time);
        }
    } else if (role == Qt::FontRole) {
        if (m_highlighted.contains(c->id)) {
            QFont f;
            f.setBold(true);
            return f;
        }
    }
    return {};
}

QVariant CommitModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case SubjectColumn: return tr("Commit");
    case AuthorColumn: return tr("Author");
    case DateColumn: return tr("Date");
    }
    return {};
}

QList<int> CommitModel::rowsWithPrefix(const QString &hexPrefix) const {
    QList<int> rows;
    const QString p = hexPrefix.toLower();
    for (int i = 0; i < m_commits.size(); ++i)
        if (!m_commits[i]->id.isPseudo() && m_commits[i]->id.hex().startsWith(p)) rows << i;
    return rows;
}

QList<Oid> CommitModel::allIds() const {
    QList<Oid> ids;
    ids.reserve(m_commits.size());
    for (const auto &c : m_commits) ids << c->id;
    return ids;
}

void CommitModel::emitRowChanged(const Oid &id) {
    int r = rowOf(id);
    if (r >= 0) emit dataChanged(index(r, 0), index(r, ColumnCount - 1));
}

void CommitModel::setHighlighted(const QSet<Oid> &ids) {
    QSet<Oid> old = m_highlighted;
    m_highlighted = ids;
    for (const Oid &id : old)
        if (!ids.contains(id)) emitRowChanged(id);
    for (const Oid &id : ids)
        if (!old.contains(id)) emitRowChanged(id);
}

void CommitModel::addHighlighted(const QList<Oid> &ids) {
    for (const Oid &id : ids) {
        if (m_highlighted.contains(id)) continue;
        m_highlighted.insert(id);
        emitRowChanged(id);
    }
}

void CommitModel::setMarked(const Oid &id) {
    Oid old = m_marked;
    m_marked = id;
    emitRowChanged(old);
    emitRowChanged(id);
}

} // namespace gitq
