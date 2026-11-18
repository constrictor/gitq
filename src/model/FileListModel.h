// The file list to the right of the diff (gitk's "Patch" / "Tree" list).
#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QStringList>

namespace gitq {

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { PathRole = Qt::UserRole + 1, IsCommentsRole, StatusRole };

    struct Entry {
        QString path;       // empty for the "Comments" entry
        QChar status;       // A/M/D/R... in patch mode, null in tree mode
    };

    explicit FileListModel(QObject *parent = nullptr);

    void setEntries(const QList<Entry> &entries, bool withComments);
    void setHighlightedPaths(const QSet<QString> &paths);
    const QList<Entry> &entries() const { return m_entries; }
    int rowOfPath(const QString &path) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QList<Entry> m_entries;
    QSet<QString> m_highlighted;
};

} // namespace gitq
