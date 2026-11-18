#include "ui/WelcomePage.h"

#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace gitq {

namespace {
constexpr int PathRole = Qt::UserRole;

QString homeRelative(const QString &path) {
    const QString home = QDir::homePath();
    if (path == home) return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/'))) return QStringLiteral("~") + path.mid(home.size());
    return path;
}
} // namespace

WelcomePage::WelcomePage(QWidget *parent) : QWidget(parent) {
    auto *column = new QWidget;
    column->setMaximumWidth(640);
    auto *cl = new QVBoxLayout(column);
    cl->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel(QStringLiteral("gitq"));
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 2.2);
    tf.setBold(true);
    title->setFont(tf);
    cl->addWidget(title);
    auto *subtitle = new QLabel(tr("Open a git repository to browse its history."));
    cl->addWidget(subtitle);
    cl->addSpacing(12);

    auto *open = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")), tr("&Open repository..."));
    open->setObjectName(QStringLiteral("openRepoButton"));
    auto *ol = new QHBoxLayout;
    ol->addWidget(open);
    ol->addStretch(1);
    cl->addLayout(ol);
    cl->addSpacing(18);

    m_recentLabel = new QLabel(tr("Recent repositories"));
    QFont rf = m_recentLabel->font();
    rf.setBold(true);
    m_recentLabel->setFont(rf);
    cl->addWidget(m_recentLabel);
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("recentList"));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setIconSize(QSize(24, 24));
    m_list->setSpacing(2);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->viewport()->setAutoFillBackground(false);
    cl->addWidget(m_list, 1);
    m_emptyLabel = new QLabel(tr("No recently opened repositories."));
    m_emptyLabel->setEnabled(false);
    cl->addWidget(m_emptyLabel);
    cl->addStretch(0);

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(24, 48, 24, 24);
    outer->addStretch(1);
    outer->addWidget(column, 4);
    outer->addStretch(1);

    connect(open, &QPushButton::clicked, this, &WelcomePage::browseRequested);
    // Opening may refill the list: emit after the item's signal has returned. A single click can
    // emit both itemClicked and itemActivated (depending on the style); open once.
    auto openItem = [this](QListWidgetItem *item) {
        if (m_openPending) return;
        m_openPending = true;
        const QString path = item->data(PathRole).toString();
        QMetaObject::invokeMethod(this, [this, path] {
            m_openPending = false;
            emit openRequested(path);
        }, Qt::QueuedConnection);
    };
    connect(m_list, &QListWidget::itemClicked, this, openItem);
    connect(m_list, &QListWidget::itemActivated, this, openItem);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_list->itemAt(pos);
        if (!item) return;
        const QString path = item->data(PathRole).toString();
        QMenu menu(this);
        menu.addAction(tr("Open"), this, [this, path] {
            QMetaObject::invokeMethod(this, [this, path] { emit openRequested(path); }, Qt::QueuedConnection);
        });
        menu.addAction(tr("Remove from list"), this, [this, path] {
            QMetaObject::invokeMethod(this, [this, path] { emit removeRequested(path); }, Qt::QueuedConnection);
        });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
}

void WelcomePage::setRecent(const QStringList &paths) {
    m_list->clear();
    const QIcon icon = QIcon::fromTheme(QStringLiteral("folder"));
    for (const QString &p : paths) {
        if (!QFileInfo::exists(p)) continue;
        auto *item = new QListWidgetItem(icon, QStringLiteral("%1\n%2").arg(QFileInfo(p).fileName(), homeRelative(p)));
        item->setData(PathRole, p);
        item->setToolTip(p);
        m_list->addItem(item);
    }
    const bool empty = m_list->count() == 0;
    m_list->setVisible(!empty);
    m_recentLabel->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
}

} // namespace gitq
