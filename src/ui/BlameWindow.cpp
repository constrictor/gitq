#include "ui/BlameWindow.h"

#include "Settings.h"
#include "git/Repository.h"

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace gitq {

BlameWindow::BlameWindow(const QString &repoPath, const QString &path, const Oid &at, int line, QWidget *parent)
    : QWidget(parent, Qt::Window), m_line(line) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Blame: %1").arg(path));
    resize(1000, 700);
    auto *l = new QVBoxLayout(this);
    m_title = new QLabel(tr("Loading blame for %1 at %2...").arg(path, at.isNull() ? QStringLiteral("HEAD") : at.shortHex(10)));
    l->addWidget(m_title);
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({tr("Commit"), tr("Author"), tr("Date"), tr("Line"), tr("Text")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setFont(Settings::instance().textFont);
    l->addWidget(m_table);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto *it = m_table->item(row, 0);
        if (it) emit commitActivated(it->data(Qt::UserRole).value<Oid>());
    });
    connect(&m_watcher, &QFutureWatcher<BlameResult>::finished, this, [this, path, at] {
        populate(m_watcher.result());
        m_title->setText(tr("Blame for %1 at %2 (double-click a line to select its commit)")
                             .arg(path, at.isNull() ? QStringLiteral("HEAD") : at.shortHex(10)));
    });
    m_watcher.setFuture(QtConcurrent::run([repoPath, path, at] {
        try {
            Repository repo = Repository::open(repoPath);
            return Blame::blameFile(repo, path, at);
        } catch (const GitError &e) {
            BlameResult r;
            r.error = e.message();
            return r;
        }
    }));
}

void BlameWindow::populate(const BlameResult &r) {
    if (!r.error.isEmpty()) {
        m_title->setText(r.error);
        return;
    }
    m_table->setRowCount(int(r.lines.size()));
    // Alternate commit blocks: the view's base colour and a light tint of the highlight colour.
    const QPalette pal = m_table->palette();
    const QColor base = pal.color(QPalette::Base), hl = pal.color(QPalette::Highlight);
    const QColor tint = QColor::fromRgbF(float(base.redF() * 0.88 + hl.redF() * 0.12),
                                         float(base.greenF() * 0.88 + hl.greenF() * 0.12),
                                         float(base.blueF() * 0.88 + hl.blueF() * 0.12));
    const QList<QColor> shades = {base, tint};
    Oid prev;
    int shade = 0;
    for (int i = 0; i < r.lines.size(); ++i) {
        const BlameLine &b = r.lines[i];
        if (b.commit != prev) {
            shade ^= 1;
            prev = b.commit;
        }
        bool first = i == 0 || r.lines[i - 1].commit != b.commit;
        QStringList cols = {first ? b.commit.shortHex(8) : QString(), first ? b.author.name : QString(),
                            first ? QDateTime::fromSecsSinceEpoch(b.author.time).toString(QStringLiteral("yyyy-MM-dd")) : QString(),
                            QString::number(i + 1), b.text};
        for (int c = 0; c < cols.size(); ++c) {
            auto *it = new QTableWidgetItem(cols[c]);
            it->setBackground(shades[shade]);
            it->setData(Qt::UserRole, QVariant::fromValue(b.commit));
            if (c < 3) it->setToolTip(QStringLiteral("%1\n%2").arg(b.commit.hex(), b.summary));
            m_table->setItem(i, c, it);
        }
    }
    m_table->resizeColumnsToContents();
    m_table->resizeRowsToContents();
    if (m_line > 0 && m_line <= m_table->rowCount()) {
        m_table->selectRow(m_line - 1);
        m_table->scrollToItem(m_table->item(m_line - 1, 0), QAbstractItemView::PositionAtCenter);
    }
}

} // namespace gitq
