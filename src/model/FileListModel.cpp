#include "model/FileListModel.h"

#include <QBrush>
#include <QFont>

namespace gitq {

FileListModel::FileListModel(QObject *parent) : QAbstractListModel(parent) {}

void FileListModel::setEntries(const QList<Entry> &entries, bool withComments) {
    beginResetModel();
    m_entries.clear();
    if (withComments) m_entries.append({QString(), QChar()});
    m_entries.append(entries);
    endResetModel();
}

void FileListModel::setHighlightedPaths(const QSet<QString> &paths) {
    m_highlighted = paths;
    if (!m_entries.isEmpty()) emit dataChanged(index(0), index(rowCount() - 1));
}

int FileListModel::rowOfPath(const QString &path) const {
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].path == path) return i;
    return -1;
}

int FileListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_entries.size()); }

QVariant FileListModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_entries.size()) return {};
    const Entry &e = m_entries[idx.row()];
    switch (role) {
    case Qt::DisplayRole: return e.path.isEmpty() ? tr("Comments") : e.path;
    case Qt::ToolTipRole: return e.path;
    case PathRole: return e.path;
    case IsCommentsRole: return e.path.isEmpty();
    case StatusRole: return e.status.isNull() ? QString() : QString(e.status);
    case Qt::FontRole:
        if (!e.path.isEmpty() && m_highlighted.contains(e.path)) {
            QFont f;
            f.setBold(true);
            return f;
        }
        break;
    case Qt::ForegroundRole:
        if (e.status == QLatin1Char('A')) return QBrush(QColor(0x00, 0x80, 0x00));
        if (e.status == QLatin1Char('D')) return QBrush(QColor(0xb0, 0x00, 0x00));
        break;
    }
    return {};
}

} // namespace gitq
