#include "ui/PrefsDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace gitq {

namespace {

QCheckBox *boolField(QFormLayout *form, const QString &label, bool &value) {
    auto *c = new QCheckBox(label);
    c->setChecked(value);
    QObject::connect(c, &QCheckBox::toggled, c, [&value](bool on) { value = on; });
    form->addRow(QString(), c);
    return c;
}

QSpinBox *intField(QFormLayout *form, const QString &label, int &value, int min, int max) {
    auto *s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(value);
    QObject::connect(s, &QSpinBox::valueChanged, s, [&value](int v) { value = v; });
    form->addRow(label, s);
    return s;
}

QLineEdit *textField(QFormLayout *form, const QString &label, QString &value) {
    auto *e = new QLineEdit(value);
    QObject::connect(e, &QLineEdit::textChanged, e, [&value](const QString &v) { value = v; });
    form->addRow(label, e);
    return e;
}

void setSwatch(QPushButton *b, const QColor &c) {
    b->setStyleSheet(QStringLiteral("background-color: %1; min-width: 4em;").arg(c.name()));
}

void colorField(QFormLayout *form, const QString &label, QColor &value, QWidget *parent) {
    auto *b = new QPushButton;
    setSwatch(b, value);
    QObject::connect(b, &QPushButton::clicked, b, [&value, b, label, parent] {
        QColor c = QColorDialog::getColor(value, parent, label);
        if (c.isValid()) {
            value = c;
            setSwatch(b, c);
        }
    });
    form->addRow(label, b);
}

void fontField(QFormLayout *form, const QString &label, QFont &value, QWidget *parent) {
    auto *b = new QPushButton(QStringLiteral("%1 %2").arg(value.family()).arg(value.pointSize()));
    QObject::connect(b, &QPushButton::clicked, b, [&value, b, label, parent] {
        bool ok = false;
        QFont f = QFontDialog::getFont(&ok, value, parent, label);
        if (ok) {
            value = f;
            b->setText(QStringLiteral("%1 %2").arg(f.family()).arg(f.pointSize()));
        }
    });
    form->addRow(label, b);
}

} // namespace

PrefsDialog::PrefsDialog(QWidget *parent) : QDialog(parent), m_s(Settings::instance()) {
    setWindowTitle(tr("Gitq preferences"));
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    tabs->addTab(generalPage(), tr("General"));
    tabs->addTab(colorsPage(), tr("Colors"));
    tabs->addTab(fontsPage(), tr("Fonts"));
    layout->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QWidget *PrefsDialog::generalPage() {
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    intField(f, tr("Maximum graph width (lines)"), m_s.maxGraphWidth, 1, 200);
    intField(f, tr("Maximum graph width (% of pane)"), m_s.maxGraphPct, 10, 100);
    boolField(f, tr("Show local changes"), m_s.showLocalChanges);
    boolField(f, tr("Auto-select SHA1"), m_s.autoSelect);
    intField(f, tr("Length of commit ID to copy"), m_s.autoSelLen, 4, 40);
    boolField(f, tr("Hide remote refs"), m_s.hideRemotes);
    boolField(f, tr("Display nearby tags/heads"), m_s.showNearTags);
    intField(f, tr("Maximum # tags/heads to show"), m_s.maxRefs, 1, 1000);
    boolField(f, tr("Limit diffs to listed paths"), m_s.limitDiffs);
    boolField(f, tr("Wrap long lines in the diff pane"), m_s.wrapDiff);
    intField(f, tr("Tab spacing"), m_s.tabStop, 1, 20);
    textField(f, tr("External diff tool"), m_s.extDiffTool);
    textField(f, tr("Date format (Qt syntax)"), m_s.dateFormat);
    boolField(f, tr("Relative dates in the commit list (\"2 hours ago\")"), m_s.relativeDates);
    return w;
}

QWidget *PrefsDialog::colorsPage() {
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    colorField(f, tr("Background"), m_s.bgColor, this);
    colorField(f, tr("Foreground"), m_s.fgColor, this);
    colorField(f, tr("Diff: old lines"), m_s.diffOldColor, this);
    colorField(f, tr("Diff: old lines bg"), m_s.diffOldBg, this);
    colorField(f, tr("Diff: new lines"), m_s.diffNewColor, this);
    colorField(f, tr("Diff: new lines bg"), m_s.diffNewBg, this);
    colorField(f, tr("Diff: hunk header"), m_s.diffHunkColor, this);
    colorField(f, tr("Marked line bg"), m_s.markBgColor, this);
    colorField(f, tr("Select bg"), m_s.selectBgColor, this);
    colorField(f, tr("Found text bg"), m_s.foundBgColor, this);
    colorField(f, tr("Current search hit bg"), m_s.currentSearchBgColor, this);
    colorField(f, tr("Local branch label"), m_s.headBgColor, this);
    colorField(f, tr("Remote branch label"), m_s.remoteBgColor, this);
    colorField(f, tr("Tag label"), m_s.tagBgColor, this);
    colorField(f, tr("File separator bg"), m_s.fileSepBgColor, this);
    colorField(f, tr("Links"), m_s.linkColor, this);
    return w;
}

QWidget *PrefsDialog::fontsPage() {
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    fontField(f, tr("Main font"), m_s.mainFont, this);
    fontField(f, tr("Diff display font"), m_s.textFont, this);
    fontField(f, tr("User interface font"), m_s.uiFont, this);
    return w;
}

} // namespace gitq
