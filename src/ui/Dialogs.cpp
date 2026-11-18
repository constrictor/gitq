#include "ui/Dialogs.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace gitq {

namespace {
QDialogButtonBox *okCancel(QDialog *d) {
    auto *b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(b, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return b;
}

QLineEdit *readOnly(const QString &text) {
    auto *e = new QLineEdit(text);
    e->setReadOnly(true);
    return e;
}
} // namespace

NameDialog::NameDialog(const QString &title, const QString &idText, const QString &nameLabel, bool withMessage,
                       QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(title);
    auto *l = new QVBoxLayout(this);
    auto *f = new QFormLayout;
    f->addRow(tr("ID:"), readOnly(idText));
    m_name = new QLineEdit;
    f->addRow(nameLabel, m_name);
    if (withMessage) {
        m_message = new QPlainTextEdit;
        m_message->setPlaceholderText(tr("Leave empty for a lightweight tag"));
        m_message->setMaximumHeight(120);
        f->addRow(tr("Tag message is optional"), m_message);
    }
    l->addLayout(f);
    l->addWidget(okCancel(this));
    m_name->setFocus();
}

QString NameDialog::name() const { return m_name->text().trimmed(); }
QString NameDialog::message() const { return m_message ? m_message->toPlainText() : QString(); }

PatchDialog::PatchDialog(const QString &title, const QString &from, const QString &to, const QString &file,
                         bool withFrom, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(title);
    auto *l = new QVBoxLayout(this);
    auto *f = new QFormLayout;
    if (withFrom) {
        m_from = new QLineEdit(from);
        f->addRow(tr("From:"), m_from);
    }
    m_to = new QLineEdit(to);
    f->addRow(withFrom ? tr("To:") : tr("ID:"), m_to);
    m_file = new QLineEdit(file);
    auto *row = new QHBoxLayout;
    row->addWidget(m_file);
    auto *browse = new QPushButton(tr("Browse..."));
    connect(browse, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getSaveFileName(this, tr("Output file"), m_file->text());
        if (!f.isEmpty()) m_file->setText(f);
    });
    row->addWidget(browse);
    f->addRow(tr("Output file:"), row);
    l->addLayout(f);
    l->addWidget(okCancel(this));
}

QString PatchDialog::from() const { return m_from ? m_from->text().trimmed() : QString(); }
QString PatchDialog::to() const { return m_to->text().trimmed(); }
QString PatchDialog::file() const { return m_file->text().trimmed(); }

ResetDialog::ResetDialog(const QString &branch, const QString &target, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Confirm reset"));
    auto *l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("Reset branch %1 to %2?").arg(branch, target)));
    m_soft = new QRadioButton(tr("Soft: Leave working tree and index untouched"));
    m_mixed = new QRadioButton(tr("Mixed: Leave working tree untouched, reset index"));
    m_hard = new QRadioButton(tr("Hard: Reset working tree and index\n(discard ALL local changes)"));
    m_mixed->setChecked(true);
    for (auto *r : {m_soft, m_mixed, m_hard}) l->addWidget(r);
    l->addWidget(okCancel(this));
}

Ops::ResetType ResetDialog::type() const {
    return m_soft->isChecked() ? Ops::Soft : m_hard->isChecked() ? Ops::Hard : Ops::Mixed;
}

RefsDialog::RefsDialog(const QList<RefInfo> &refs, QWidget *parent) : QDialog(parent), m_refs(refs) {
    setWindowTitle(tr("Tags and heads"));
    resize(420, 500);
    auto *l = new QVBoxLayout(this);
    m_list = new QListWidget;
    l->addWidget(m_list);
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Filter")));
    m_filter = new QLineEdit(QStringLiteral("*"));
    row->addWidget(m_filter);
    l->addLayout(row);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(buttons);
    connect(m_filter, &QLineEdit::textChanged, this, &RefsDialog::refilter);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        emit refChosen(it->data(Qt::UserRole).value<Oid>());
    });
    refilter();
}

void RefsDialog::refilter() {
    m_list->clear();
    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(
        m_filter->text().isEmpty() ? QStringLiteral("*") : m_filter->text(), QRegularExpression::UnanchoredWildcardConversion));
    QList<RefInfo> sorted = m_refs;
    std::sort(sorted.begin(), sorted.end(), [](const RefInfo &a, const RefInfo &b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.name < b.name;
    });
    for (const RefInfo &r : sorted) {
        if (!re.match(r.name).hasMatch()) continue;
        QString label = r.kind == RefInfo::Tag ? QStringLiteral("tag  ") : r.kind == RefInfo::RemoteBranch
                                                                              ? QStringLiteral("rem  ")
                                                                              : QStringLiteral("head ");
        auto *it = new QListWidgetItem(label + r.name, m_list);
        it->setData(Qt::UserRole, QVariant::fromValue(r.commit));
    }
}

QString keyBindingsText() {
    return QStringLiteral(
        "Gitq key bindings:\n\n"
        "Ctrl-Q\t\tQuit\n"
        "Ctrl-W\t\tClose window\n"
        "Home\t\tMove to first commit\n"
        "End\t\tMove to last commit\n"
        "Up, p, k\tMove up one commit\n"
        "Down, n, j\tMove down one commit\n"
        "Left, z, h\tGo back in history list\n"
        "Right, x, l\tGo forward in history list\n"
        "Ctrl-<n>\tGo to n-th parent of current commit in history list\n"
        "PageUp\tMove up one page in commit list\n"
        "PageDown\tMove down one page in commit list\n"
        "Ctrl-Home\tScroll to top of commit list\n"
        "Ctrl-End\tScroll to bottom of commit list\n"
        "Ctrl-Up\tScroll commit list up one line\n"
        "Ctrl-Down\tScroll commit list down one line\n"
        "Ctrl-PageUp\tScroll commit list up one page\n"
        "Ctrl-PageDown\tScroll commit list down one page\n"
        "Shift-Up\tFind backwards (upwards, later commits)\n"
        "Shift-Down\tFind forwards (downwards, earlier commits)\n"
        "Delete\t\tScroll diff view up one page\n"
        "b\t\tScroll diff view to previous file\n"
        "Backspace\tScroll diff view up one page\n"
        "Space\t\tScroll diff view down one page\n"
        "u\t\tScroll diff view up 18 lines\n"
        "d\t\tScroll diff view down 18 lines\n"
        "Ctrl-F\t\tFind\n"
        "Ctrl-G\t\tMove to next find hit\n"
        "Return\tMove to next find hit\n"
        "g\t\tGo to commit\n"
        "/\t\tFocus the search box\n"
        "?\t\tMove to previous find hit\n"
        "f\t\tScroll diff view to next file\n"
        "Ctrl-S\t\tSearch for next hit in diff view\n"
        "Ctrl-R\t\tSearch for previous hit in diff view\n"
        "Ctrl-KP+\tIncrease font size\n"
        "Ctrl-plus\tIncrease font size\n"
        "Ctrl-KP-\tDecrease font size\n"
        "Ctrl-minus\tDecrease font size\n"
        "F5\t\tUpdate\n"
        "Shift-F5\tReload\n"
        "F2\t\tList references\n"
        "F4\t\tEdit view\n"
        "Shift-F4\tNew view\n");
}

} // namespace gitq
