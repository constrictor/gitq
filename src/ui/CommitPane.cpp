#include "ui/CommitPane.h"

#include "Settings.h"
#include "git/GitCli.h"
#include "git/Repository.h"
#include "ui/Glyphs.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

namespace gitq {

// ---- CommitMessageEdit ----------------------------------------------------------------------

CommitMessageEdit::CommitMessageEdit(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("commitMessage"));
    setPlaceholderText(tr("Commit message\n\nA short summary on the first line, then a blank line and the details."));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabChangesFocus(true);
    applyFonts();
}

void CommitMessageEdit::applyFonts() {
    setFont(Settings::instance().textFont);
    viewport()->update();
}

void CommitMessageEdit::paintEvent(QPaintEvent *e) {
    QPlainTextEdit::paintEvent(e);
    // A guide at the conventional maximum line length.
    QPainter p(viewport());
    const double cw = QFontMetricsF(font()).horizontalAdvance(QLatin1Char('m'));
    const double x = contentOffset().x() + document()->documentMargin() + kGuideColumn * cw;
    QColor c = palette().color(QPalette::Text);
    c.setAlphaF(0.12f);
    p.setPen(c);
    p.drawLine(QPointF(x + 0.5, 0), QPointF(x + 0.5, viewport()->height()));
}

bool CommitMessageEdit::event(QEvent *e) {
    // Typing must never trigger the window's single-key shortcuts (gitk's list navigation keys).
    if (e->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const Qt::KeyboardModifiers mods = ke->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
        if (mods == Qt::NoModifier && (!ke->text().isEmpty() || ke->key() == Qt::Key_Backspace ||
                                       ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Home ||
                                       ke->key() == Qt::Key_End || ke->key() == Qt::Key_PageUp ||
                                       ke->key() == Qt::Key_PageDown || ke->key() == Qt::Key_Up ||
                                       ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Left ||
                                       ke->key() == Qt::Key_Right)) {
            e->accept();
            return true;
        }
    }
    return QPlainTextEdit::event(e);
}

// ---- CommitPane -----------------------------------------------------------------------------

CommitPane::CommitPane(const Repository *repo, QWidget *parent) : QWidget(parent), m_repo(repo) {
    setObjectName(QStringLiteral("commitPaneContent"));
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(6, 6, 6, 6);
    l->setSpacing(4);

    m_branchLabel = new QLabel;
    m_branchLabel->setObjectName(QStringLiteral("commitBranch"));
    m_branchLabel->setTextFormat(Qt::RichText);
    l->addWidget(m_branchLabel);

    m_edit = new CommitMessageEdit;
    l->addWidget(m_edit, 1);

    m_info = new QLabel;
    m_info->setObjectName(QStringLiteral("commitInfo"));
    m_info->setTextFormat(Qt::RichText);
    l->addWidget(m_info);

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_amend = new QCheckBox(tr("&Amend"));
    m_amend->setObjectName(QStringLiteral("amendCheck"));
    m_amend->setToolTip(tr("Replace the last commit with one made of it and the staged changes, using the message below"));
    m_signOff = new QCheckBox(tr("&Sign off"));
    m_signOff->setObjectName(QStringLiteral("signOffCheck"));
    m_signOff->setToolTip(tr("Add a Signed-off-by trailer (git commit --signoff)"));
    m_signOff->setChecked(Settings::instance().commitSignOff);
    m_commitButton = new QPushButton(tr("Commit"));
    m_commitButton->setObjectName(QStringLiteral("commitButton"));
    m_commitButton->setToolTip(tr("Commit the staged changes (Ctrl+Return)"));
    m_commitButton->setDefault(true);
    row->addWidget(m_amend);
    row->addWidget(m_signOff);
    row->addStretch(1);
    row->addWidget(m_commitButton);
    l->addLayout(row);

    m_draftTimer = new QTimer(this);
    m_draftTimer->setSingleShot(true);
    m_draftTimer->setInterval(500);
    connect(m_draftTimer, &QTimer::timeout, this, &CommitPane::saveDraftNow);

    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_loading && !m_amend->isChecked()) m_draftTimer->start();
        updateState();
    });
    connect(m_amend, &QCheckBox::toggled, this, &CommitPane::onAmendToggled);
    connect(m_signOff, &QCheckBox::toggled, this, [](bool on) {
        Settings::instance().commitSignOff = on;
        Settings::instance().save();
    });
    connect(m_commitButton, &QPushButton::clicked, this, &CommitPane::commit);
    for (QKeySequence seq : {QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto *s = new QShortcut(seq, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, &CommitPane::commit);
    }

    // The draft of the previous session, or the prepared message of a merge in progress.
    QString text = Ops::loadDraft(*m_repo);
    if (text.trimmed().isEmpty()) text = Ops::mergeMessage(*m_repo);
    m_loading = true;
    m_edit->setPlainText(text);
    m_loading = false;
    updateState();
}

CommitPane::~CommitPane() {
    if (m_draftTimer->isActive()) saveDraftNow();
}

void CommitPane::saveDraftNow() {
    m_draftTimer->stop();
    if (!m_amend->isChecked()) Ops::saveDraft(*m_repo, m_edit->toPlainText());
}

void CommitPane::setHead(const QString &branch, bool unborn, bool published) {
    m_branch = branch;
    m_unborn = unborn;
    m_published = published;
    if (unborn && m_amend->isChecked()) m_amend->setChecked(false);
    updateState();
}

void CommitPane::setStaged(const StagedSummary &s) {
    m_staged = s;
    updateState();
}

QString CommitPane::message() const { return m_edit->toPlainText(); }

void CommitPane::setMessage(const QString &text) { m_edit->setPlainText(text); }

bool CommitPane::amend() const { return m_amend->isChecked(); }

void CommitPane::setAmend(bool on) { m_amend->setChecked(on); }

void CommitPane::applyFonts() { m_edit->applyFonts(); }

void CommitPane::onAmendToggled(bool on) {
    m_loading = true;
    if (on) {
        // Edit HEAD's message; the draft comes back when amending is turned off.
        saveDraftNow();
        m_stashedDraft = m_edit->toPlainText();
        m_edit->setPlainText(Ops::headMessage(*m_repo));
    } else {
        m_edit->setPlainText(m_stashedDraft);
        m_stashedDraft.clear();
    }
    m_loading = false;
    updateState();
}

void CommitPane::updateState() {
    const QString esc = m_branch.toHtmlEscaped();
    m_branchLabel->setText(m_branch.isEmpty() ? tr("Commit on a <b>detached HEAD</b>")
                                              : tr("Commit to <b>%1</b>").arg(esc));
    m_amend->setEnabled(!m_unborn && !m_busy);

    // Summary line length (50 recommended, 72 at most) and a missing blank second line.
    const QTextBlock first = m_edit->document()->firstBlock();
    const int len = int(first.text().size());
    const QTextBlock second = first.next();
    QStringList parts;
    const QColor warn = Settings::mix(removedColor(), palette().color(QPalette::WindowText), 0.3);
    if (len > 0) {
        QString s = tr("Summary %1").arg(len);
        if (len > CommitMessageEdit::kGuideColumn) s = QStringLiteral("<span style='color:%1'>%2 — too long</span>").arg(warn.name(), s);
        else if (len > 50) s = QStringLiteral("<span style='color:%1'>%2 (over 50)</span>").arg(warn.name(), s);
        parts << s;
        if (second.isValid() && !second.text().trimmed().isEmpty())
            parts << QStringLiteral("<span style='color:%1'>%2</span>").arg(warn.name(), tr("leave line 2 blank"));
    }
    const bool merging = QFile::exists(QDir(m_repo->path()).filePath(QStringLiteral("MERGE_HEAD")));
    if (m_staged.files > 0)
        parts << (m_staged.files == 1 ? tr("1 file staged") : tr("%1 files staged").arg(m_staged.files)) +
                     QStringLiteral(" <span style='color:%1'>+%2</span> <span style='color:%3'>−%4</span>")
                         .arg(addedColor().name())
                         .arg(m_staged.added)
                         .arg(removedColor().name())
                         .arg(m_staged.removed);
    else if (merging)
        parts << tr("merge in progress");
    else
        parts << tr("nothing staged");
    if (m_amend->isChecked() && m_published)
        parts << QStringLiteral("<span style='color:%1'>%2</span>").arg(warn.name(), tr("HEAD is already pushed"));
    m_info->setText(parts.join(QStringLiteral(" · ")));

    const bool hasMessage = !m_edit->toPlainText().trimmed().isEmpty();
    m_commitButton->setEnabled(!m_busy && hasMessage && (m_staged.files > 0 || m_amend->isChecked() || merging));
    m_commitButton->setText(m_busy ? tr("Committing…") : m_amend->isChecked() ? tr("Amend") : tr("Commit"));
    m_edit->setReadOnly(m_busy);
}

void CommitPane::commit() {
    if (!m_commitButton->isEnabled()) return;
    if (m_amend->isChecked() && m_published) {
        const auto answer = QMessageBox::question(
            this, tr("Amend a pushed commit?"),
            tr("The last commit is already on %1's upstream branch. Amending it rewrites published history, "
               "and pushing will need --force.\n\nAmend anyway?")
                .arg(m_branch),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }
    CommitRequest req;
    req.message = m_edit->toPlainText();
    req.amend = m_amend->isChecked();
    req.signOff = m_signOff->isChecked();
    m_busy = true;
    updateState();
    emit statusMessage(tr("Committing…"), 0);
    GitCli::runAsync(m_repo->workdir(), Ops::commitArgs(req), req.message.toUtf8(), this, [this](const GitRun &r) {
        m_busy = false;
        const OpResult res = Ops::commitResult(*m_repo, r.ok(), r.ok() ? QString::fromUtf8(r.out) : r.errorText());
        if (res.ok) {
            m_loading = true;
            m_stashedDraft.clear();
            m_amend->blockSignals(true);
            m_amend->setChecked(false);
            m_amend->blockSignals(false);
            m_edit->clear();
            m_loading = false;
            m_draftTimer->stop();
            Ops::saveDraft(*m_repo, QString());
        }
        updateState();
        emit committed(res);
    });
}

} // namespace gitq
