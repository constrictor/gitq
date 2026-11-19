// The file list to the right of the diff (gitk's "Patch" / "Tree" list). Optionally grouped by
// folder: a row for each folder, followed by its files.
#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QStringList>

namespace gitq {

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        IsCommentsRole,
        StatusRole,
        IsFolderRole,   // a folder row (grouped)
        AddedRole,      // lines added, -1 if unknown
        RemovedRole,    // lines removed, -1 if unknown
        BinaryRole,
        NameRole,       // what to show: the path, or the file name under its folder when grouped
    };

    struct Entry {
        QString path;       // empty for the "Comments" entry and folder rows
        QChar status;       // A/M/D/R... in patch mode, null in tree mode
        int added = -1, removed = -1;
        bool binary = false;
        QString folder;     // folder rows: the folder's path
    };

    explicit FileListModel(QObject *parent = nullptr);

    void setEntries(const QList<Entry> &entries, bool withComments);
    void setGrouped(bool on);
    bool grouped() const { return m_grouped; }
    void setHighlightedPaths(const QSet<QString> &paths);
    // The rows shown (including the "Comments" row and folder rows).
    const QList<Entry> &entries() const { return m_rows; }
    int rowOfPath(const QString &path) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    void rebuild();

    QList<Entry> m_files;   // as given
    bool m_withComments = false;
    bool m_grouped = false;
    QList<Entry> m_rows;
    QSet<QString> m_highlighted;
};

} // namespace gitq
