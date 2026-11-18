// Per-file blame window (replaces gitk's "git gui blame" launcher).
#pragma once

#include "git/Blame.h"

#include <QFutureWatcher>
#include <QWidget>

class QTableWidget;
class QLabel;

namespace gitq {

class BlameWindow : public QWidget {
    Q_OBJECT
public:
    BlameWindow(const QString &repoPath, const QString &path, const Oid &at, int line, QWidget *parent = nullptr);

signals:
    void commitActivated(const gitq::Oid &id);

private:
    void populate(const BlameResult &r);

    QLabel *m_title;
    QTableWidget *m_table;
    int m_line;
    QFutureWatcher<BlameResult> m_watcher;
};

} // namespace gitq
