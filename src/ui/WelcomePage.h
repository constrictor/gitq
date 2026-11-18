// Shown instead of the panes when no repository is open: recently opened repositories and an
// "Open repository" button.
#pragma once

#include <QWidget>

class QLabel;
class QListWidget;

namespace gitq {

class WelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget *parent = nullptr);

    // Paths of recently opened repositories, most recent first; missing ones are left out.
    void setRecent(const QStringList &paths);

signals:
    void openRequested(const QString &path);
    void browseRequested();
    void removeRequested(const QString &path);

private:
    QListWidget *m_list;
    QLabel *m_recentLabel, *m_emptyLabel;
    bool m_openPending = false;
};

} // namespace gitq
