#include "ui/ViewDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace gitq {

ViewDialog::ViewDialog(const ViewDef &view, bool isNew, QWidget *parent) : QDialog(parent) {
    setWindowTitle(isNew ? tr("Gitq view definition") : tr("Gitq: edit view %1").arg(view.name));
    auto *layout = new QVBoxLayout(this);

    auto *top = new QFormLayout;
    m_name = new QLineEdit;
    top->addRow(tr("View Name"), m_name);
    m_remember = new QCheckBox(tr("Remember this view"));
    top->addRow(QString(), m_remember);
    layout->addLayout(top);

    auto *refsBox = new QGroupBox(tr("References (space separated list):"));
    auto *rl = new QVBoxLayout(refsBox);
    m_refs = new QLineEdit;
    rl->addWidget(m_refs);
    auto *rflags = new QHBoxLayout;
    m_allRefs = new QCheckBox(tr("All refs"));
    m_branches = new QCheckBox(tr("All (local) branches"));
    m_tags = new QCheckBox(tr("All tags"));
    m_remotes = new QCheckBox(tr("All remote-tracking branches"));
    for (auto *c : {m_allRefs, m_branches, m_tags, m_remotes}) rflags->addWidget(c);
    rl->addLayout(rflags);
    layout->addWidget(refsBox);

    auto *infoBox = new QGroupBox(tr("Commit Info (regular expressions):"));
    auto *il = new QFormLayout(infoBox);
    m_author = new QLineEdit;
    m_committer = new QLineEdit;
    m_grep = new QLineEdit;
    il->addRow(tr("Author:"), m_author);
    il->addRow(tr("Committer:"), m_committer);
    il->addRow(tr("Commit Message:"), m_grep);
    m_allMatch = new QCheckBox(tr("Matches all Commit Info criteria"));
    il->addRow(QString(), m_allMatch);
    layout->addWidget(infoBox);

    auto *dateBox = new QGroupBox(tr("Commit Dates (\"2 weeks ago\", \"2009-03-17 15:27:38\", \"March 17, 2009 15:27:38\"):"));
    auto *dl = new QFormLayout(dateBox);
    m_since = new QLineEdit;
    m_until = new QLineEdit;
    dl->addRow(tr("Since:"), m_since);
    dl->addRow(tr("Until:"), m_until);
    layout->addWidget(dateBox);

    auto *limBox = new QGroupBox(tr("Limit and/or skip a number of revisions (positive integer):"));
    auto *ll = new QHBoxLayout(limBox);
    m_maxCount = new QSpinBox;
    m_maxCount->setRange(0, 100000000);
    m_maxCount->setSpecialValueText(tr("no limit"));
    m_skip = new QSpinBox;
    m_skip->setRange(0, 100000000);
    ll->addWidget(new QLabel(tr("Number to show:")));
    ll->addWidget(m_maxCount);
    ll->addWidget(new QLabel(tr("Number to skip:")));
    ll->addWidget(m_skip);
    layout->addWidget(limBox);

    auto *miscBox = new QGroupBox(tr("Miscellaneous options:"));
    auto *ml = new QHBoxLayout(miscBox);
    m_dateOrder = new QCheckBox(tr("Strictly sort by date"));
    m_firstParent = new QCheckBox(tr("Limit to first parent"));
    m_noMerges = new QCheckBox(tr("No merges"));
    m_fullHistory = new QCheckBox(tr("Full history"));
    for (auto *c : {m_dateOrder, m_firstParent, m_noMerges, m_fullHistory}) ml->addWidget(c);
    layout->addWidget(miscBox);

    auto *extra = new QFormLayout;
    m_extraArgs = new QLineEdit;
    extra->addRow(tr("Additional arguments to git log:"), m_extraArgs);
    layout->addLayout(extra);

    layout->addWidget(new QLabel(tr("Enter files and directories to include, one per line:")));
    m_paths = new QPlainTextEdit;
    m_paths->setMaximumHeight(100);
    layout->addWidget(m_paths);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Apply)->setText(tr("Apply (F5)"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { done(2); });
    layout->addWidget(buttons);

    load(view);
}

void ViewDialog::load(const ViewDef &v) {
    m_name->setText(v.name);
    m_remember->setChecked(v.remember);
    QStringList refs, extra, paths;
    bool inPaths = false;
    for (const QString &a : v.args) {
        QString val;
        auto opt = [&](const char *name) {
            QString p = QLatin1String(name) + QLatin1Char('=');
            if (a.startsWith(p)) {
                val = a.mid(p.size());
                return true;
            }
            return false;
        };
        if (inPaths) paths << a;
        else if (a == QLatin1String("--")) inPaths = true;
        else if (a == QLatin1String("--all")) m_allRefs->setChecked(true);
        else if (a == QLatin1String("--branches")) m_branches->setChecked(true);
        else if (a == QLatin1String("--tags")) m_tags->setChecked(true);
        else if (a == QLatin1String("--remotes")) m_remotes->setChecked(true);
        else if (opt("--author")) m_author->setText(val);
        else if (opt("--committer")) m_committer->setText(val);
        else if (opt("--grep")) m_grep->setText(val);
        else if (a == QLatin1String("--all-match")) m_allMatch->setChecked(true);
        else if (opt("--since") || opt("--after")) m_since->setText(val);
        else if (opt("--until") || opt("--before")) m_until->setText(val);
        else if (opt("--max-count")) m_maxCount->setValue(val.toInt());
        else if (opt("--skip")) m_skip->setValue(val.toInt());
        else if (a == QLatin1String("--date-order")) m_dateOrder->setChecked(true);
        else if (a == QLatin1String("--first-parent")) m_firstParent->setChecked(true);
        else if (a == QLatin1String("--no-merges")) m_noMerges->setChecked(true);
        else if (a == QLatin1String("--full-history")) m_fullHistory->setChecked(true);
        else if (a.startsWith(QLatin1Char('-'))) extra << a;
        else refs << a;
    }
    m_refs->setText(refs.join(QLatin1Char(' ')));
    m_extraArgs->setText(extra.join(QLatin1Char(' ')));
    m_paths->setPlainText(paths.join(QLatin1Char('\n')));
}

ViewDef ViewDialog::view() const {
    ViewDef v;
    v.name = m_name->text().trimmed();
    v.remember = m_remember->isChecked();
    QStringList &a = v.args;
    if (m_allRefs->isChecked()) a << QStringLiteral("--all");
    if (m_branches->isChecked()) a << QStringLiteral("--branches");
    if (m_tags->isChecked()) a << QStringLiteral("--tags");
    if (m_remotes->isChecked()) a << QStringLiteral("--remotes");
    if (!m_author->text().isEmpty()) a << QStringLiteral("--author=") + m_author->text();
    if (!m_committer->text().isEmpty()) a << QStringLiteral("--committer=") + m_committer->text();
    if (!m_grep->text().isEmpty()) a << QStringLiteral("--grep=") + m_grep->text();
    if (m_allMatch->isChecked()) a << QStringLiteral("--all-match");
    if (!m_since->text().trimmed().isEmpty()) a << QStringLiteral("--since=") + m_since->text().trimmed();
    if (!m_until->text().trimmed().isEmpty()) a << QStringLiteral("--until=") + m_until->text().trimmed();
    if (m_maxCount->value() > 0) a << QStringLiteral("--max-count=%1").arg(m_maxCount->value());
    if (m_skip->value() > 0) a << QStringLiteral("--skip=%1").arg(m_skip->value());
    if (m_dateOrder->isChecked()) a << QStringLiteral("--date-order");
    if (m_firstParent->isChecked()) a << QStringLiteral("--first-parent");
    if (m_noMerges->isChecked()) a << QStringLiteral("--no-merges");
    if (m_fullHistory->isChecked()) a << QStringLiteral("--full-history");
    a << QProcess::splitCommand(m_extraArgs->text());
    a << m_refs->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList paths;
    for (const QString &p : m_paths->toPlainText().split(QLatin1Char('\n')))
        if (!p.trimmed().isEmpty()) paths << p.trimmed();
    if (!paths.isEmpty()) a << QStringLiteral("--") << paths;
    if (v.name.isEmpty()) v.name = tr("View");
    return v;
}

} // namespace gitq
