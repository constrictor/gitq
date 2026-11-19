#include "model/FileListModel.h"

#include <QFont>
#include <QMap>

namespace gitq {

FileListModel::FileListModel(QObject *parent) : QAbstractListModel(parent) {}

void FileListModel::setEntries(const QList<Entry> &entries, bool withComments) {
    m_files = entries;
    m_withComments = withComments;
    rebuild();
}

void FileListModel::setGrouped(bool on) {
    if (on == m_grouped) return;
    m_grouped = on;
    rebuild();
}

void FileListModel::rebuild() {
    beginResetModel();
    m_rows.clear();
    if (m_withComments) m_rows.append(Entry());
    if (!m_grouped) {
        m_rows.append(m_files);
    } else {
        // Files at the top level first, then each folder followed by its files, folders sorted.
        QList<Entry> top;
        QMap<QString, QList<Entry>> folders;
        for (const Entry &e : std::as_const(m_files)) {
            const int slash = int(e.path.lastIndexOf(QLatin1Char('/')));
            if (slash < 0) top.append(e);
            else folders[e.path.left(slash)].append(e);
        }
        m_rows.append(top);
        for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
            Entry folder;
            folder.folder = it.key();
            m_rows.append(folder);
            m_rows.append(it.value());
        }
    }
    endResetModel();
}

void FileListModel::setHighlightedPaths(const QSet<QString> &paths) {
    m_highlighted = paths;
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(rowCount() - 1));
}

int FileListModel::rowOfPath(const QString &path) const {
    if (path.isEmpty()) return -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].path == path) return i;
    return -1;
}

int FileListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }

QVariant FileListModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_rows.size()) return {};
    const Entry &e = m_rows[idx.row()];
    const bool folder = !e.folder.isEmpty();
    switch (role) {
    case Qt::DisplayRole:
        if (folder) return e.folder;
        return e.path.isEmpty() ? tr("Comments") : e.path;
    case NameRole:
        if (folder) return e.folder + QLatin1Char('/');
        if (e.path.isEmpty()) return tr("Comments");
        return m_grouped ? e.path.mid(e.path.lastIndexOf(QLatin1Char('/')) + 1) : e.path;
    case Qt::ToolTipRole: return folder ? e.folder : e.path;
    case PathRole: return e.path;
    case IsCommentsRole: return e.path.isEmpty() && !folder;
    case IsFolderRole: return folder;
    case StatusRole: return e.status.isNull() ? QString() : QString(e.status);
    case AddedRole: return e.added;
    case RemovedRole: return e.removed;
    case BinaryRole: return e.binary;
    case Qt::FontRole:
        if (!e.path.isEmpty() && m_highlighted.contains(e.path)) {
            QFont f;
            f.setBold(true);
            return f;
        }
        break;
    }
    return {};
}

} // namespace gitq
