#include "ui/DiffView.h"

#include "Settings.h"
#include "model/CommitModel.h"
#include "ui/Glyphs.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPlainTextDocumentLayout>
#include <QTextObjectInterface>
#include <QToolTip>
#include <QDateTime>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>
#include <QUrl>

namespace gitq {

namespace {

QTextCharFormat plainFormat() {
    QTextCharFormat f;
    f.setForeground(Settings::instance().fgColor);
    return f;
}

const QRegularExpression &hunkRe() {
    static const QRegularExpression re(QStringLiteral("^@@ -(\\d+)(?:,\\d+)? \\+(\\d+)(?:,\\d+)? @@"));
    return re;
}

} // namespace

namespace {

enum ObjectKind { SpacerObject = 1, PillObject, IdChipObject };
constexpr int kObjectType = QTextFormat::UserObject + 1;
constexpr int kKindProp = QTextFormat::UserProperty + 1;
constexpr int kIndexProp = QTextFormat::UserProperty + 2;

// Initials of a name: first letters of the first and last words.
QString initials(const QString &name) {
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) return QStringLiteral("?");
    QString s = words.first().left(1);
    if (words.size() > 1) s += words.last().left(1);
    return s.toUpper();
}

// A stable, pleasant colour per person.
QColor avatarColor(const QString &key) {
    const uint h = qHash(key.toLower());
    return QColor::fromHsl(int(h % 360), 120 + int((h >> 9) % 60), Settings::darkTheme() ? 95 : 110);
}

// Plain-text document layout whose inline objects are centred on the text line instead of standing
// on the baseline.
class CenteredObjectLayout : public QPlainTextDocumentLayout {
public:
    using QPlainTextDocumentLayout::QPlainTextDocumentLayout;

protected:
    void resizeInlineObject(QTextInlineObject item, int posInDocument, const QTextFormat &format) override {
        QPlainTextDocumentLayout::resizeInlineObject(item, posInDocument, format);
        const QFontMetricsF fm(format.toCharFormat().font());
        const qreal h = item.height();
        const qreal ascent = (h + fm.ascent() - fm.descent()) / 2;
        item.setAscent(ascent);
        item.setDescent(h - ascent);
    }
};

} // namespace

// Draws the card's inline objects: the indent past the avatar, ref labels and the id chip.
class HeaderObjects : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    explicit HeaderObjects(DiffView *view) : QObject(view), m_view(view) {}

    static int indent(const QFont &font) { return int(QFontMetrics(font).height() * 2.6) + 18; }

    QSizeF intrinsicSize(QTextDocument *, int, const QTextFormat &format) override {
        const QFont font = format.toCharFormat().font();
        const QFontMetrics fm(font);
        const int h = fm.height() + 4;
        switch (format.intProperty(kKindProp)) {
        case SpacerObject: return QSizeF(indent(Settings::instance().uiFont), 1);
        case PillObject: {
            const int i = format.intProperty(kIndexProp);
            if (i < 0 || i >= m_view->m_cardPills.size()) return {};
            return QSizeF(RefPillPainter(font, h).width(m_view->m_cardPills[i]) + 4, h);
        }
        case IdChipObject: return QSizeF(chipWidth(font), h);
        }
        return {};
    }

    void drawObject(QPainter *p, const QRectF &rect, QTextDocument *, int, const QTextFormat &format) override {
        const QFont font = format.toCharFormat().font();
        const int h = int(rect.height());
        switch (format.intProperty(kKindProp)) {
        case PillObject: {
            const int i = format.intProperty(kIndexProp);
            if (i >= 0 && i < m_view->m_cardPills.size())
                RefPillPainter(font, h).paint(p, m_view->m_cardPills[i], int(rect.left()), int(rect.top()));
            break;
        }
        case IdChipObject: {
            const Settings &st = Settings::instance();
            const QColor fg = st.fgColor, bg = Settings::mix(st.bgColor, st.fgColor, 0.08);
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            const QRectF box = rect.adjusted(0.5, 0.5, -0.5, -0.5);
            p->setPen(QPen(Settings::mix(bg, fg, 0.25), 1));
            p->setBrush(bg);
            p->drawRoundedRect(box, h * 0.3, h * 0.3);
            const QFont mono = chipFont(font);
            const int pad = h / 3;
            p->setFont(mono);
            p->setPen(fg);
            const QString id = m_view->m_cardId.left(10);
            const int tw = QFontMetrics(mono).horizontalAdvance(id);
            p->drawText(QRectF(rect.left() + pad, rect.top(), tw + 1, h), Qt::AlignVCenter | Qt::AlignLeft, id);
            const double icon = h * 0.6;
            drawGlyph(p, Glyph::Copy, QRectF(rect.left() + pad + tw + pad * 0.7, rect.top() + (h - icon) / 2, icon, icon),
                      Settings::mix(fg, bg, 0.35));
            p->restore();
            break;
        }
        default: break;
        }
    }

private:
    QFont chipFont(const QFont &font) const {
        QFont mono = Settings::instance().textFont;
        mono.setPointSizeF(font.pointSizeF() > 0 ? font.pointSizeF() * 0.95 : mono.pointSizeF());
        return mono;
    }
    int chipWidth(const QFont &font) const {
        const int h = QFontMetrics(font).height() + 4;
        const int pad = h / 3;
        return pad + QFontMetrics(chipFont(font)).horizontalAdvance(m_view->m_cardId.left(10)) + int(pad * 0.7) +
               int(h * 0.6) + pad;
    }

    DiffView *m_view;
};

// The line numbers left of the diff; painted by the view, which knows the blocks.
class LineGutter : public QWidget {
public:
    explicit LineGutter(DiffView *view) : QWidget(view), m_view(view) {}

protected:
    void paintEvent(QPaintEvent *e) override { m_view->paintGutter(e); }
    void contextMenuEvent(QContextMenuEvent *e) override {
        emit m_view->diffContextMenu(m_view->lineInfoAt(QPoint(0, e->pos().y())), e->globalPos());
    }
    void mousePressEvent(QMouseEvent *e) override {
        // Select the line, like clicking at the start of it would.
        if (e->button() != Qt::LeftButton) return;
        QTextCursor c = m_view->cursorForPosition(QPoint(0, e->pos().y()));
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        m_view->setTextCursor(c);
    }

private:
    DiffView *m_view;
};

// Colours body lines from the per-block styles recorded while rendering.
class DiffHighlighter : public QSyntaxHighlighter {
public:
    explicit DiffHighlighter(DiffView *view) : QSyntaxHighlighter(view->document()), m_view(view) {}

protected:
    void highlightBlock(const QString &text) override {
        const int n = currentBlock().blockNumber();
        if (n < 0 || n >= m_view->m_style.size()) return;
        const quint8 style = m_view->m_style[n];
        if (style == DiffView::HeaderStyle) return;
        const Settings &st = Settings::instance();
        auto fg = [](const QColor &c) {
            QTextCharFormat f;
            f.setForeground(c);
            return f;
        };
        switch (style) {
        case DiffView::OldStyle: setFormat(0, int(text.size()), fg(st.diffOldColor)); break;
        case DiffView::NewStyle: setFormat(0, int(text.size()), fg(st.diffNewColor)); break;
        case DiffView::HunkStyle: setFormat(0, int(text.size()), fg(st.diffHunkColor)); break;
        case DiffView::ErrorStyle: setFormat(0, int(text.size()), fg(Qt::red)); break;
        case DiffView::FileSepStyle: {
            // Taller line for the header bar painted over it.
            QTextCharFormat f;
            QFont font = st.uiFont;
            font.setBold(true);
            if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * 1.35);
            f.setFont(font);
            f.setForeground(Qt::transparent);
            setFormat(0, int(text.size()), f);
            return;
        }
        default: break;
        }
        auto it = m_view->m_wordRanges.constFind(n);
        if (it == m_view->m_wordRanges.cend()) return;
        const bool colorWords = m_view->m_wordMode == DiffView::ColorWords;
        for (const auto &r : *it) {
            QTextCharFormat f = fg(r.kind == DiffView::OldStyle ? st.diffOldColor : st.diffNewColor);
            if (colorWords) f.setBackground(r.kind == DiffView::OldStyle ? st.diffOldBg : st.diffNewBg);
            setFormat(r.start, r.len, f);
        }
    }

private:
    DiffView *m_view;
};

DiffView::DiffView(QWidget *parent) : QPlainTextEdit(parent) {
    document()->setDocumentLayout(new CenteredObjectLayout(document()));
    document()->documentLayout()->registerHandler(kObjectType, new HeaderObjects(this));
    new DiffHighlighter(this);
    m_gutter = new LineGutter(this);
    connect(this, &QPlainTextEdit::updateRequest, m_gutter, [this] { m_gutter->update(); });
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setMouseTracking(true);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &DiffView::onScroll);
    applyFonts();
}

void DiffView::applyFonts() {
    const Settings &st = Settings::instance();
    setFont(st.textFont);
    setTabStopDistance(QFontMetricsF(st.textFont).horizontalAdvance(QLatin1Char(' ')) * qMax(1, st.tabStop));
    setLineWrapMode(st.wrapDiff ? WidgetWidth : NoWrap);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, st.bgColor);
    pal.setColor(QPalette::Text, st.fgColor);
    setPalette(pal);
}

void DiffView::clearAll() {
    clear();
    m_meta.clear();
    m_style.clear();
    m_wordRanges.clear();
    m_pending.clear();
    m_fileStarts.clear();
    m_headerEnd = 0;
    m_cardEnd = 0;
    m_avatarText.clear();
    m_cardPills.clear();
    m_cardId.clear();
    m_singleNumberColumn = false;
    m_nearbyBlock = -1;
    m_lastTopFile.clear();
    m_currentSearchHit = QTextCursor();
    setExtraSelections({});
    updateGutter();
}

void DiffView::appendLine(const QString &text, const QTextCharFormat &fmt, const DiffLineInfo &meta,
                          const QTextBlockFormat &bfmt) {
    QTextCursor cur(document());
    cur.movePosition(QTextCursor::End);
    if (m_meta.isEmpty()) cur.setBlockFormat(bfmt);
    else cur.insertBlock(bfmt);
    cur.insertText(text, fmt);
    m_meta.append(meta);
    m_style.append(HeaderStyle);
}

void DiffView::appendLink(QTextCursor &cur, const QString &label, const Oid &id) {
    appendLinkTo(cur, label, QStringLiteral("commit:") + id.hex());
}

void DiffView::appendLinkTo(QTextCursor &cur, const QString &label, const QString &href) {
    QTextCharFormat f;
    f.setForeground(Settings::instance().linkColor);
    f.setFontUnderline(true);
    f.setAnchor(true);
    f.setAnchorHref(href);
    cur.insertText(label, f);
}

QTextCharFormat DiffView::cardFormat(double scale, bool bold, bool dim) const {
    const Settings &st = Settings::instance();
    QTextCharFormat f;
    QFont font = st.uiFont;
    if (scale != 1.0) {
        if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * scale);
        else font.setPixelSize(int(font.pixelSize() * scale));
    }
    font.setBold(bold);
    f.setFont(font);
    f.setForeground(dim ? Settings::mix(st.fgColor, st.bgColor, 0.4) : st.fgColor);
    return f;
}

void DiffView::insertObject(QTextCursor &cur, int kind, int index, const QString &href) {
    QTextCharFormat f = cardFormat();
    f.setObjectType(kObjectType);
    f.setProperty(kKindProp, kind);
    f.setProperty(kIndexProp, index);
    if (!href.isEmpty()) {
        f.setAnchor(true);
        f.setAnchorHref(href);
    }
    cur.insertText(QString(QChar::ObjectReplacementCharacter), f);
}

QTextCursor DiffView::appendCardLine(const QTextCharFormat &fmt, bool indent) {
    appendLine(QString(), fmt);
    QTextCursor cur(document());
    cur.movePosition(QTextCursor::End);
    cur.setBlockCharFormat(fmt);
    if (indent && !m_avatarText.isEmpty()) insertObject(cur, SpacerObject, 0);
    cur.setCharFormat(fmt);
    return cur;
}

void DiffView::showHeader(const CommitInfo &c, const QList<RefInfo> &refs, const QString &headBranch,
                          const QList<Link> &parents, const QList<Link> &children, const QString &dateFormat) {
    clearAll();
    const QTextCharFormat plain = plainFormat(), text = cardFormat(), dim = cardFormat(1.0, false, true);
    auto date = [&](const Person &p) {
        const QString abs = QDateTime::fromSecsSinceEpoch(p.time).toString(dateFormat);
        const QString rel = CommitModel::relativeTime(p.time, QDateTime::currentSecsSinceEpoch());
        return rel.isEmpty() ? abs : QStringLiteral("%1 (%2)").arg(rel, abs);
    };

    if (!c.id.isPseudo()) {
        m_avatarText = initials(c.author.name);
        m_avatarColor = avatarColor(c.author.email.isEmpty() ? c.author.name : c.author.email);
        m_cardId = c.id.hex();
    }
    // A thin first line pads the top of the card.
    QTextCharFormat pad = cardFormat(0.45);
    appendCardLine(pad, false);

    QTextCursor cur = appendCardLine(cardFormat(1.3, true));
    cur.insertText(c.subject);
    if (!c.id.isPseudo()) {
        cur = appendCardLine(text);
        cur.insertText(c.author.name, text);
        if (!c.author.email.isEmpty()) cur.insertText(QStringLiteral(" <%1>").arg(c.author.email), dim);
        cur.insertText(QStringLiteral("  ·  ") + date(c.author), dim);
        const bool sameCommitter = c.committer.name == c.author.name && c.committer.email == c.author.email;
        if (!sameCommitter || qAbs(c.committer.time - c.author.time) > 60) {
            cur = appendCardLine(dim);
            cur.insertText(tr("Committed by "), dim);
            cur.insertText(sameCommitter ? tr("the author") : c.committer.nameEmail(), text);
            cur.insertText(QStringLiteral("  ·  ") + date(c.committer), dim);
        }

        // Commit id chip (click to copy) and ref labels.
        cur = appendCardLine(text);
        insertObject(cur, IdChipObject, 0, QStringLiteral("copy:") + c.id.hex());
        m_cardPills = groupRefPills(refs, headBranch);
        for (int i = 0; i < m_cardPills.size(); ++i) {
            cur.insertText(QStringLiteral(" "), text);
            insertObject(cur, PillObject, i);
        }
    }

    auto linkLines = [&](const QString &label, const QList<Link> &links) {
        for (const Link &l : links) {
            QTextCursor lc = appendCardLine(text);
            lc.insertText(label, dim);
            QTextCharFormat linkFmt = text;
            linkFmt.setForeground(Settings::instance().linkColor);
            linkFmt.setFontUnderline(true);
            linkFmt.setAnchor(true);
            linkFmt.setAnchorHref(QStringLiteral("commit:") + l.first.hex());
            lc.insertText(l.first.isPseudo() ? (l.first.isWorkdir() ? tr("Local uncommitted changes")
                                                                    : tr("Local changes in index"))
                                             : l.first.shortHex(10),
                          linkFmt);
            if (!l.second.isEmpty()) lc.insertText(QStringLiteral("  ") + l.second, text);
        }
    };
    linkLines(tr("Parent: "), parents);
    linkLines(tr("Child: "), children);
    m_nearbyBlock = int(m_meta.size());
    appendCardLine(pad, false);
    m_cardEnd = int(m_meta.size());

    // The message after the subject line, indented like git log does.
    if (!c.id.isPseudo()) {
        QStringList body = c.message().split(QLatin1Char('\n'));
        body.removeFirst();
        while (!body.isEmpty() && body.first().trimmed().isEmpty()) body.removeFirst();
        while (!body.isEmpty() && body.last().trimmed().isEmpty()) body.removeLast();
        if (!body.isEmpty()) {
            appendLine(QString(), plain);
            for (const QString &line : body) appendLine(QStringLiteral("    ") + line, plain);
        }
    }
    appendLine(QString(), plain);
    m_headerEnd = int(m_meta.size());
    updateExtraSelections();
    scrollToTop();
}

void DiffView::showRangeHeader(const QString &text) {
    clearAll();
    const QTextCharFormat pad = cardFormat(0.45), fmt = plainFormat();
    appendCardLine(pad, false);
    for (const QString &line : text.split(QLatin1Char('\n'))) appendCardLine(fmt, false).insertText(line, fmt);
    appendCardLine(pad, false);
    m_cardEnd = int(m_meta.size());
    appendLine(QString(), plainFormat());
    m_headerEnd = int(m_meta.size());
}

void DiffView::showNearby(const NearbyRefs &n, int maxRefs) {
    if (m_nearbyBlock < 0) return;
    QList<QPair<QString, QString>> lines;
    auto fmt = [&](const QString &label, const QStringList &names) {
        if (names.isEmpty()) return;
        QStringList shown = names.mid(0, maxRefs);
        QString s = shown.join(QStringLiteral(", "));
        if (names.size() > maxRefs) s += QStringLiteral(", ...");
        lines.append({label, s});
    };
    fmt(tr("Branches: "), n.branches);
    fmt(tr("Follows: "), n.follows);
    fmt(tr("Precedes: "), n.precedes);
    if (lines.isEmpty()) return;

    const int at = m_nearbyBlock;
    QTextBlock block = document()->findBlockByNumber(at - 1);
    if (!block.isValid()) return;
    QTextCursor cur(block);
    cur.movePosition(QTextCursor::EndOfBlock);
    const int keep = verticalScrollBar()->value();
    const QTextCharFormat text = cardFormat(), dim = cardFormat(1.0, false, true);
    for (const auto &l : lines) {
        cur.insertBlock(QTextBlockFormat(), text);
        if (!m_avatarText.isEmpty()) insertObject(cur, SpacerObject, 0);
        cur.insertText(l.first, dim);
        cur.insertText(l.second, text);
    }
    const int k = int(lines.size());
    for (int i = 0; i < k; ++i) {
        m_meta.insert(at, DiffLineInfo());
        m_style.insert(at, HeaderStyle);
    }
    QHash<int, QList<WordRange>> words;
    for (auto it = m_wordRanges.cbegin(); it != m_wordRanges.cend(); ++it) words.insert(it.key() + k, it.value());
    m_wordRanges = words;
    QMap<int, FileHeader> shifted;
    for (auto it = m_fileStarts.cbegin(); it != m_fileStarts.cend(); ++it) shifted.insert(it.key() + k, it.value());
    m_fileStarts = shifted;
    m_headerEnd += k;
    m_cardEnd += k;
    m_nearbyBlock = -1;
    verticalScrollBar()->setValue(keep);
    updateExtraSelections();
}

int DiffView::gutterWidth() const {
    if (m_gutterDigits == 0) return 0;
    const int digit = QFontMetrics(Settings::instance().textFont).horizontalAdvance(QLatin1Char('9'));
    const int column = m_gutterDigits * digit + 10;
    return (m_singleNumberColumn ? column : 2 * column) + 6;
}

void DiffView::updateGutter() {
    int maxLine = 0;
    for (const DiffLineInfo &m : std::as_const(m_meta)) maxLine = qMax(maxLine, qMax(m.oldLine, m.newLine));
    m_gutterDigits = maxLine > 0 ? qMax(3, int(QString::number(maxLine).size())) : 0;
    setViewportMargins(gutterWidth(), 0, 0, 0);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(cr.left(), cr.top(), gutterWidth(), cr.height());
    m_gutter->setVisible(m_gutterDigits > 0);
    m_gutter->update();
}

void DiffView::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(cr.left(), cr.top(), gutterWidth(), cr.height());
}

void DiffView::paintGutter(QPaintEvent *e) {
    const Settings &st = Settings::instance();
    QPainter p(m_gutter);
    const int w = m_gutter->width();
    const QColor gutterBg = Settings::mix(st.bgColor, st.fgColor, Settings::darkTheme() ? 0.04 : 0.025);
    p.fillRect(e->rect(), gutterBg);
    // The separator from the text; the card and header bars, painted next, run across it.
    p.setPen(Settings::mix(gutterBg, st.fgColor, 0.12));
    p.drawLine(QPointF(w - 0.5, e->rect().top()), QPointF(w - 0.5, e->rect().bottom() + 1));
    const QColor cardBg = Settings::mix(st.bgColor, st.fgColor, Settings::darkTheme() ? 0.06 : 0.035);
    const QColor number = Settings::mix(st.fgColor, st.bgColor, 0.55);
    const QFont font = st.textFont;
    p.setFont(font);
    const int digit = QFontMetrics(font).horizontalAdvance(QLatin1Char('9'));
    const int column = m_gutterDigits * digit + 10;
    const QPointF offset = contentOffset();
    auto headerBar = [&](const QRectF &r) {
        p.fillRect(r, st.fileSepBgColor);
        p.setPen(Settings::mix(st.fileSepBgColor, st.fgColor, 0.18));
        p.drawLine(QPointF(0, r.top() + 0.5), QPointF(w, r.top() + 0.5));
        p.drawLine(QPointF(0, r.bottom() - 0.5), QPointF(w, r.bottom() - 0.5));
    };
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
        const QRectF r = blockBoundingGeometry(b).translated(offset);
        if (r.top() > e->rect().bottom()) break;
        const QRectF row(0, r.top(), w, r.height());
        const int n = b.blockNumber();
        if (n < m_cardEnd) {
            p.fillRect(row, cardBg);
            if (n == m_cardEnd - 1) {
                p.setPen(Settings::mix(cardBg, st.fgColor, 0.15));
                p.drawLine(QPointF(0, r.bottom() - 0.5), QPointF(w, r.bottom() - 0.5));
            }
            continue;
        }
        if (m_fileStarts.contains(n)) {
            headerBar(row);
            continue;
        }
        if (n >= m_meta.size() || m_meta[n].kind.isNull()) continue;
        const DiffLineInfo &m = m_meta[n];
        p.setPen(number);
        const QFontMetrics fm(font);
        const int lineH = fm.height();
        const QRectF first(0, r.top(), column - 4, lineH);
        if (m_singleNumberColumn) {
            p.drawText(first, Qt::AlignRight | Qt::AlignVCenter, QString::number(m.newLine));
        } else {
            if (m.oldLine > 0) p.drawText(first, Qt::AlignRight | Qt::AlignVCenter, QString::number(m.oldLine));
            if (m.newLine > 0)
                p.drawText(QRectF(column, r.top(), column - 4, lineH), Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(m.newLine));
        }
    }
    FileHeader h;
    const QRectF sticky = stickyHeader(&h);
    if (!sticky.isNull()) headerBar(QRectF(0, sticky.top(), w, sticky.height()));
}

void DiffView::paintEvent(QPaintEvent *e) {
    const Settings &st = Settings::instance();
    {
        // The header card's background, under the text.
        QPainter p(viewport());
        const QPointF offset = contentOffset();
        const QColor cardBg = Settings::mix(st.bgColor, st.fgColor, Settings::darkTheme() ? 0.06 : 0.035);
        for (QTextBlock b = firstVisibleBlock(); b.isValid() && b.blockNumber() < m_cardEnd; b = b.next()) {
            const QRectF r = blockBoundingGeometry(b).translated(offset);
            if (r.top() > e->rect().bottom()) break;
            p.fillRect(QRectF(0, r.top(), viewport()->width(), r.height()), cardBg);
            if (b.blockNumber() == m_cardEnd - 1) {
                p.setPen(Settings::mix(cardBg, st.fgColor, 0.15));
                p.drawLine(QPointF(0, r.bottom() - 0.5), QPointF(viewport()->width(), r.bottom() - 0.5));
            }
        }
    }
    QPlainTextEdit::paintEvent(e);
    {
        // File header bars over their (transparent) text, and the sticky one at the top.
        QPainter p(viewport());
        const QPointF offset = contentOffset();
        const int first = firstVisibleBlock().blockNumber();
        for (auto it = m_fileStarts.lowerBound(first); it != m_fileStarts.cend(); ++it) {
            const QTextBlock b = document()->findBlockByNumber(it.key());
            if (!b.isValid()) break;
            const QRectF r = blockBoundingGeometry(b).translated(offset);
            if (r.top() > viewport()->height()) break;
            paintFileHeader(&p, QRectF(0, r.top(), viewport()->width(), r.height()), it.value());
        }
        FileHeader h;
        const QRectF sticky = stickyHeader(&h);
        if (!sticky.isNull()) {
            paintFileHeader(&p, sticky, h);
            QLinearGradient shadow(0, sticky.bottom(), 0, sticky.bottom() + 4);
            shadow.setColorAt(0, QColor(0, 0, 0, Settings::darkTheme() ? 90 : 40));
            shadow.setColorAt(1, QColor(0, 0, 0, 0));
            p.fillRect(QRectF(0, sticky.bottom(), viewport()->width(), 4), shadow);
        }
    }
    if (m_avatarText.isEmpty() || m_cardEnd < 3) return;
    // The avatar: the author's initials in a circle beside the subject and author lines.
    const QTextBlock subject = document()->findBlockByNumber(1), author = document()->findBlockByNumber(2);
    if (!subject.isValid() || !author.isValid()) return;
    const QPointF offset = contentOffset();
    const QRectF r1 = blockBoundingGeometry(subject).translated(offset), r2 = blockBoundingGeometry(author).translated(offset);
    const double top = r1.top(), bottom = r2.bottom();
    const double d = qMin(bottom - top, HeaderObjects::indent(st.uiFont) - 18.0);
    const QRectF circle(document()->documentMargin() + 8, (top + bottom - d) / 2, d, d);
    if (circle.bottom() < 0) return;
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(m_avatarColor);
    p.drawEllipse(circle);
    QFont f = st.uiFont;
    f.setBold(true);
    f.setPixelSize(qMax(8, int(d * 0.4)));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(circle, Qt::AlignCenter, m_avatarText);
}

QRectF DiffView::stickyHeader(FileHeader *out) const {
    const QTextBlock top = firstVisibleBlock();
    if (!top.isValid() || m_fileStarts.isEmpty()) return {};
    const QPointF offset = contentOffset();
    // The header of the file the top line belongs to, unless that header is itself fully visible.
    auto it = m_fileStarts.upperBound(top.blockNumber());
    if (it == m_fileStarts.cbegin()) return {};
    auto cur = std::prev(it);
    const QTextBlock hb = document()->findBlockByNumber(cur.key());
    if (!hb.isValid()) return {};
    const QRectF hr = blockBoundingGeometry(hb).translated(offset);
    if (hr.top() >= 0) return {};
    double y = 0;
    if (it != m_fileStarts.cend()) {   // pushed up by the next file's header
        const QTextBlock nb = document()->findBlockByNumber(it.key());
        if (nb.isValid()) y = qMin(0.0, blockBoundingGeometry(nb).translated(offset).top() - hr.height());
    }
    if (out) *out = cur.value();
    return QRectF(0, y, viewport()->width(), hr.height());
}

void DiffView::paintFileHeader(QPainter *p, const QRectF &r, const FileHeader &h) const {
    const Settings &st = Settings::instance();
    const QColor bg = st.fileSepBgColor, fg = st.fgColor;
    const QColor dim = Settings::mix(fg, bg, 0.45);
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->fillRect(r, bg);
    p->setPen(Settings::mix(bg, fg, 0.18));
    p->drawLine(QPointF(r.left(), r.top() + 0.5), QPointF(r.right(), r.top() + 0.5));
    p->drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));

    QFont font = st.uiFont;
    const QFontMetricsF fm(font);
    double x = r.left() + 8 + document()->documentMargin();
    const double cy = r.center().y();

    // Status badge.
    if (!h.status.isNull()) {
        QColor c;
        switch (h.status.unicode()) {
        case 'A': c = QColor(0x2e, 0x9d, 0x4f); break;
        case 'D': c = QColor(0xd9, 0x3f, 0x3f); break;
        case 'R': c = QColor(0x8e, 0x5c, 0xd9); break;
        case 'C': c = QColor(0x13, 0x9c, 0x9c); break;
        case 'T': c = QColor(0x80, 0x80, 0x80); break;
        default: c = QColor(0xd0, 0x8a, 0x2e); break;
        }
        const double s = qMin(r.height() - 6, fm.height() + 2);
        const QRectF badge(x, cy - s / 2, s, s);
        p->setPen(Qt::NoPen);
        p->setBrush(c);
        p->drawRoundedRect(badge, s * 0.25, s * 0.25);
        QFont bf = font;
        bf.setBold(true);
        bf.setPixelSize(qMax(7, int(s * 0.62)));
        p->setFont(bf);
        p->setPen(Qt::white);
        p->drawText(badge, Qt::AlignCenter, QString(h.status));
        x += s + 8;
    }

    // Right side: line counts and a five-block bar, or "binary".
    double right = r.right() - 10;
    p->setFont(font);
    if (h.binary) {
        const QString t = tr("binary");
        right -= fm.horizontalAdvance(t);
        p->setPen(dim);
        p->drawText(QRectF(right, r.top(), fm.horizontalAdvance(t) + 1, r.height()), Qt::AlignVCenter, t);
        right -= 10;
    } else if (h.added || h.removed) {
        const QColor plus = Settings::darkTheme() ? QColor(0x56, 0xd3, 0x64) : QColor(0x1f, 0x88, 0x3d);
        const QColor minus = Settings::darkTheme() ? QColor(0xff, 0x7b, 0x72) : QColor(0xcf, 0x22, 0x2e);
        if (!h.submodule) {
            const int total = h.added + h.removed;
            const int green = total ? qRound(5.0 * h.added / total) : 0;
            const double bs = qMax(5.0, fm.height() * 0.5);
            for (int i = 4; i >= 0; --i) {
                right -= bs;
                const QRectF sq(right, cy - bs / 2, bs - 1.5, bs - 1.5);
                p->fillRect(sq, i < green ? plus : minus);
                right -= 0.5;
            }
            right -= 8;
        }
        const QString m = h.submodule ? tr("%1 commits").arg(h.added + h.removed) : QStringLiteral("−%1").arg(h.removed);
        const QString a = QStringLiteral("+%1").arg(h.added);
        if (h.submodule) {
            right -= fm.horizontalAdvance(m);
            p->setPen(dim);
            p->drawText(QRectF(right, r.top(), fm.horizontalAdvance(m) + 1, r.height()), Qt::AlignVCenter, m);
        } else {
            right -= fm.horizontalAdvance(m);
            p->setPen(minus);
            p->drawText(QRectF(right, r.top(), fm.horizontalAdvance(m) + 1, r.height()), Qt::AlignVCenter, m);
            right -= fm.horizontalAdvance(a) + 6;
            p->setPen(plus);
            p->drawText(QRectF(right, r.top(), fm.horizontalAdvance(a) + 1, r.height()), Qt::AlignVCenter, a);
        }
        right -= 12;
    }
    if (h.submodule) {
        const QString t = tr("submodule");
        right -= fm.horizontalAdvance(t);
        p->setPen(dim);
        p->drawText(QRectF(right, r.top(), fm.horizontalAdvance(t) + 1, r.height()), Qt::AlignVCenter, t);
        right -= 12;
    }

    // The path: directory dimmed, file name in bold.
    const double avail = right - x;
    if (avail > 10) {
        QFont bold = font;
        bold.setBold(true);
        const QFontMetricsF bm(bold);
        const int slash = int(h.title.lastIndexOf(QLatin1Char('/')));
        QString dir = h.title.contains(QStringLiteral(" → ")) || slash < 0 ? QString() : h.title.left(slash + 1);
        QString name = dir.isEmpty() ? h.title : h.title.mid(slash + 1);
        const double nw = bm.horizontalAdvance(name);
        if (nw >= avail) {
            name = bm.elidedText(name, Qt::ElideLeft, avail);
            dir.clear();
        } else {
            dir = fm.elidedText(dir, Qt::ElideLeft, avail - nw);
        }
        p->setFont(font);
        p->setPen(dim);
        const double dw = fm.horizontalAdvance(dir);
        p->drawText(QRectF(x, r.top(), dw + 1, r.height()), Qt::AlignVCenter, dir);
        p->setFont(bold);
        p->setPen(fg);
        p->drawText(QRectF(x + dw, r.top(), avail - dw, r.height()), Qt::AlignVCenter, name);
    }
    p->restore();
}

void DiffView::mousePressEvent(QMouseEvent *e) {
    // A click on the sticky header goes to the top of its file.
    FileHeader h;
    const QRectF sticky = stickyHeader(&h);
    if (e->button() == Qt::LeftButton && !sticky.isNull() && sticky.contains(e->position())) {
        scrollToFile(h.path);
        e->accept();
        return;
    }
    QPlainTextEdit::mousePressEvent(e);
}

bool DiffView::viewportEvent(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const QString href = anchorAt(he->pos());
        if (href.startsWith(QLatin1String("copy:"))) {
            QToolTip::showText(he->globalPos(), tr("Copy the commit ID %1").arg(href.mid(5)), viewport());
            return true;
        }
        if (href.startsWith(QLatin1String("commit:"))) {
            QToolTip::showText(he->globalPos(), href.mid(7), viewport());
            return true;
        }
        QToolTip::hideText();
    }
    return QPlainTextEdit::viewportEvent(e);
}

void DiffView::appendDiff(const DiffResult &diff) {
    const int keep = verticalScrollBar()->value();
    if (!diff.error.isEmpty()) addBodyLine(diff.error, ErrorStyle, {});
    for (const FileDiff &f : diff.files) {
        if (f.submodule >= 0 && f.submodule < diff.submodules.size() && diff.submodules[f.submodule].detailed)
            renderSubmodule(diff.submodules[f.submodule]);
        else
            renderFile(f);
    }
    flushBody();
    updateGutter();
    verticalScrollBar()->setValue(keep);
    updateExtraSelections();
    onScroll();
}

void DiffView::addBodyLine(const QString &text, quint8 style, const DiffLineInfo &meta, const QList<WordRange> &words) {
    if (!words.isEmpty()) m_wordRanges.insert(int(m_meta.size()), words);
    m_pending << text;
    m_meta.append(meta);
    m_style.append(style);
}

void DiffView::flushBody() {
    if (m_pending.isEmpty()) return;
    QTextCursor cur(document());
    cur.movePosition(QTextCursor::End);
    const bool empty = m_meta.size() == m_pending.size();   // nothing but the pending lines
    if (!empty) cur.insertBlock();
    cur.insertText(m_pending.join(QLatin1Char('\n')), plainFormat());
    m_pending.clear();
}

void DiffView::renderFile(const FileDiff &f) {
    const int n = qMax(1, f.numParents);
    FileHeader header;
    header.path = f.path();
    header.title = f.status == QLatin1Char('R') || f.status == QLatin1Char('C')
                       ? QStringLiteral("%1 → %2").arg(f.oldPath, f.newPath)
                       : f.path();
    header.status = f.status;
    header.binary = f.binary;
    bool hunks = false;
    for (const QString &line : f.lines) {
        if (!hunks) {
            hunks = line.startsWith(QLatin1String("@@"));
            continue;
        }
        if (line.startsWith(QLatin1String("@@"))) continue;
        const QString marks = line.left(n);
        if (marks.contains(QLatin1Char('+'))) ++header.added;
        else if (marks.contains(QLatin1Char('-'))) ++header.removed;
    }
    m_fileStarts.insert(int(m_meta.size()), header);
    DiffLineInfo fileMeta;
    fileMeta.path = f.path();
    addBodyLine(header.title, FileSepStyle, fileMeta);

    bool inHunks = false;
    int oldNo = 0, newNo = 0;
    QStringList dels, adds;
    const bool words = m_wordMode != LineDiff && n == 1 && m_mode == ShowDiff;
    auto flushRun = [&] {
        if (dels.isEmpty() && adds.isEmpty()) return;
        renderWordRun(dels, adds, f.path(), oldNo, newNo);
        dels.clear();
        adds.clear();
    };

    for (const QString &line : f.lines) {
        if (!inHunks) {
            if (line.startsWith(QLatin1String("@@"))) {
                inHunks = true;
            } else {
                if (line.startsWith(QLatin1String("diff --")) || line.startsWith(QLatin1String("--- ")) ||
                    line.startsWith(QLatin1String("+++ ")))
                    continue;
                addBodyLine(line, PlainStyle, fileMeta);
                continue;
            }
        }
        if (line.startsWith(QLatin1String("@@"))) {
            flushRun();
            auto m = hunkRe().match(line);
            if (m.hasMatch()) {
                oldNo = m.captured(1).toInt();
                newNo = m.captured(2).toInt();
            } else if (n > 1) {   // combined: "@@@ -a,b -c,d +e,f @@@", only the result's lines count
                static const QRegularExpression plus(QStringLiteral(" \\+(\\d+)(?:,\\d+)? @@"));
                auto pm = plus.match(line);
                if (pm.hasMatch()) newNo = pm.captured(1).toInt();
            }
            addBodyLine(line, HunkStyle, fileMeta);
            continue;
        }
        if (line.startsWith(QLatin1Char('\\'))) {
            addBodyLine(line, PlainStyle, fileMeta);
            continue;
        }
        const QString marks = line.left(n);
        const bool isDel = marks.contains(QLatin1Char('-'));
        const bool isAdd = marks.contains(QLatin1Char('+'));
        if (words && (isDel || isAdd)) {
            if (isAdd && !isDel) adds << line.mid(1);
            else dels << line.mid(1);
            continue;
        }
        flushRun();
        DiffLineInfo meta;
        meta.path = f.path();
        meta.kind = isDel ? QLatin1Char('-') : isAdd ? QLatin1Char('+') : QLatin1Char(' ');
        if (n == 1) {
            if (!isAdd) meta.oldLine = oldNo++;
            if (!isDel) meta.newLine = newNo++;
        } else if (!isDel) {
            meta.newLine = newNo++;
        }
        if (m_mode == ShowOld && isAdd && !isDel) continue;
        if (m_mode == ShowNew && isDel) continue;
        QString text = m_mode == ShowDiff ? line : line.mid(n);
        addBodyLine(text, isDel ? OldStyle : isAdd ? NewStyle : PlainStyle, meta);
    }
    flushRun();
}

// A submodule entry: what it moved from and to, the commits in between (as links that open the
// submodule in a tab) and links to show its files and to open it.
void DiffView::renderSubmodule(const SubmoduleChange &s) {
    FileHeader header;
    header.path = header.title = s.path;
    header.status = s.status;
    header.submodule = true;
    header.added = int(s.added.size()) + s.moreAdded;
    header.removed = int(s.removed.size()) + s.moreRemoved;
    m_fileStarts.insert(int(m_meta.size()), header);
    DiffLineInfo meta;
    meta.path = s.path;
    addBodyLine(s.path, FileSepStyle, meta);
    flushBody();

    const QTextCharFormat plain = plainFormat();
    const QString path = QString::fromLatin1(QUrl::toPercentEncoding(s.path));
    const bool available = !s.repoPath.isEmpty();
    auto commitLink = [&](QTextCursor &cur, const Oid &id) {
        if (available) appendLinkTo(cur, id.shortHex(), QStringLiteral("subcommit:%1:%2").arg(id.hex(), path));
        else cur.insertText(id.shortHex(), plain);
    };
    auto line = [&] {
        appendLine(QString(), plain, meta);
        QTextCursor cur(document());
        cur.movePosition(QTextCursor::End);
        return cur;
    };

    QTextCursor cur = line();
    cur.insertText(tr("Submodule %1 ").arg(s.path), plain);
    if (s.oldId.isNull() && !s.newId.isNull()) {
        cur.insertText(tr("added at "), plain);
        commitLink(cur, s.newId);
    } else if (s.newId.isNull() && !s.oldId.isNull()) {
        cur.insertText(tr("removed (was at "), plain);
        commitLink(cur, s.oldId);
        cur.insertText(QStringLiteral(")"), plain);
    } else {
        commitLink(cur, s.oldId);
        cur.insertText(s.removed.isEmpty() ? QStringLiteral("..") : QStringLiteral("..."), plain);
        commitLink(cur, s.newId);
        if (s.newIsWorkdir) cur.insertText(tr(" + work tree"), plain);
        if (!s.removed.isEmpty()) cur.insertText(tr(" (rewind)"), plain);
    }
    if (m_submoduleToggles && s.expandable) {
        cur.insertText(QStringLiteral("   "), plain);
        appendLinkTo(cur, s.expanded ? tr("[hide files]") : tr("[show files]"), QStringLiteral("subtoggle:") + path);
    }
    if (available) {
        cur.insertText(QStringLiteral("   "), plain);
        appendLinkTo(cur, tr("[open in tab]"), QStringLiteral("subcommit::") + path);
    }

    auto commits = [&](const QList<CommitLink> &list, int more, const QString &mark) {
        for (const CommitLink &c : list) {
            QTextCursor l = line();
            l.insertText(QStringLiteral("  %1 ").arg(mark), plain);
            commitLink(l, c.first);
            l.insertText(QLatin1Char(' ') + c.second, plain);
        }
        if (more > 0) line().insertText(tr("  %1 ... and %n more", nullptr, more).arg(mark), plain);
    };
    commits(s.added, s.moreAdded, QStringLiteral(">"));
    commits(s.removed, s.moreRemoved, QStringLiteral("<"));
    for (const QString &note : s.notes) line().insertText(QStringLiteral("  (%1)").arg(note), plain);
}

void DiffView::renderWordRun(const QStringList &dels, const QStringList &adds, const QString &path, int &oldNo,
                             int &newNo) {
    const QList<WordSeg> segs = DiffEngine::wordDiff(dels.join(QLatin1Char('\n')), adds.join(QLatin1Char('\n')));
    QString text = QStringLiteral(" ");
    QList<WordRange> ranges;
    auto emitLine = [&] {
        DiffLineInfo meta;
        meta.path = path;
        if (adds.isEmpty()) {
            meta.kind = QLatin1Char('-');
            meta.oldLine = oldNo;
        } else {
            meta.kind = QLatin1Char('+');
            meta.newLine = newNo++;
        }
        addBodyLine(text, PlainStyle, meta, ranges);
        text = QStringLiteral(" ");
        ranges.clear();
    };
    auto append = [&](const QString &t, quint8 kind) {
        if (t.isEmpty()) return;
        if (kind != PlainStyle) ranges.append({int(text.size()), int(t.size()), kind});
        text += t;
    };
    bool pendingContent = false;
    for (const WordSeg &s : segs) {
        if (s.kind == WordSeg::Removed) {
            QString t = s.text;
            t.replace(QLatin1Char('\n'), QLatin1Char(' '));
            if (m_wordMode == MarkupWords) t = QStringLiteral("[-") + t + QStringLiteral("-]");
            append(t, OldStyle);
            pendingContent = true;
            continue;
        }
        const QStringList parts = s.text.split(QLatin1Char('\n'));
        for (int i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                emitLine();
                pendingContent = false;
            }
            if (parts[i].isEmpty()) continue;
            QString t = parts[i];
            if (s.kind == WordSeg::Added && m_wordMode == MarkupWords) t = QStringLiteral("{+") + t + QStringLiteral("+}");
            append(t, s.kind == WordSeg::Added ? NewStyle : PlainStyle);
            pendingContent = true;
        }
    }
    if (pendingContent || !adds.isEmpty()) emitLine();
    oldNo += int(dels.size());
}

void DiffView::showFileContents(const QString &path, const QString &text) {
    clearAll();
    DiffLineInfo meta;
    meta.path = path;
    FileHeader header;
    header.path = header.title = path;
    m_fileStarts.insert(0, header);
    addBodyLine(path, FileSepStyle, meta);
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    int no = 1;
    for (const QString &l : lines) {
        DiffLineInfo m;
        m.path = path;
        m.kind = QLatin1Char(' ');
        m.oldLine = m.newLine = no++;
        addBodyLine(l, PlainStyle, m);
    }
    m_singleNumberColumn = true;
    flushBody();
    updateGutter();
    updateExtraSelections();
    scrollToTop();
}

void DiffView::scrollToTop() {
    verticalScrollBar()->setValue(0);
    QTextCursor c(document());
    c.movePosition(QTextCursor::Start);
    setTextCursor(c);
}

void DiffView::scrollToFile(const QString &path) {
    for (auto it = m_fileStarts.cbegin(); it != m_fileStarts.cend(); ++it) {
        if (it.value().path != path) continue;
        QTextBlock b = document()->findBlockByNumber(it.key());
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        QTextCursor c(b);
        setTextCursor(c);
        ensureCursorVisible();
        return;
    }
}

bool DiffView::scrollToLine(const QString &path, int line, bool newSide) {
    for (int i = 0; i < m_meta.size(); ++i) {
        const DiffLineInfo &m = m_meta[i];
        if (m.path == path && (newSide ? m.newLine : m.oldLine) == line) {
            QTextBlock b = document()->findBlockByNumber(i);
            QTextCursor c(b);
            c.select(QTextCursor::BlockUnderCursor);
            setTextCursor(c);
            centerCursor();
            return true;
        }
    }
    return false;
}

QStringList DiffView::files() const {
    QStringList out;
    for (const FileHeader &h : m_fileStarts) out << h.path;
    return out;
}

QString DiffView::fileAtTop() const {
    int top = firstVisibleBlock().blockNumber();
    auto it = m_fileStarts.upperBound(top);
    if (it == m_fileStarts.cbegin()) return {};
    --it;
    return it.value().path;
}

void DiffView::onScroll() {
    viewport()->update();   // the sticky header must not be scrolled along with the text
    QString f = fileAtTop();
    if (f != m_lastTopFile) {
        m_lastTopFile = f;
        emit fileAtTopChanged(f);
    }
}

DiffLineInfo DiffView::lineInfoAt(const QPoint &pos) const {
    int b = cursorForPosition(pos).blockNumber();
    return b >= 0 && b < m_meta.size() ? m_meta[b] : DiffLineInfo();
}

void DiffView::setSearchText(const QString &text) {
    m_searchText = text;
    m_currentSearchHit = QTextCursor();
    updateExtraSelections();
}

bool DiffView::searchNext(bool backward) {
    if (m_searchText.isEmpty()) return false;
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    QTextCursor from = m_currentSearchHit.isNull() ? textCursor() : m_currentSearchHit;
    if (m_currentSearchHit.isNull() && !backward) {
        from = cursorForPosition(QPoint(0, 0));
        from.movePosition(QTextCursor::StartOfBlock);
    }
    QTextCursor hit = document()->find(m_searchText, from, flags);
    if (hit.isNull()) {
        QTextCursor wrap(document());
        wrap.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        hit = document()->find(m_searchText, wrap, flags);
    }
    if (hit.isNull()) return false;
    m_currentSearchHit = hit;
    QTextCursor c = hit;
    c.clearSelection();
    setTextCursor(c);
    ensureCursorVisible();
    updateExtraSelections();
    return true;
}

void DiffView::setFindSpec(const FindSpec &spec) {
    m_findSpec = spec;
    updateExtraSelections();
}

void DiffView::updateExtraSelections() {
    const Settings &st = Settings::instance();
    QList<QTextEdit::ExtraSelection> sels;
    if (m_findSpec.type == FindSpec::Containing && !m_findSpec.text.isEmpty()) {
        QTextBlock b = document()->begin();
        for (int i = 0; b.isValid() && i < m_headerEnd; ++i, b = b.next()) {
            for (const auto &r : Search::matchRanges(b.text(), m_findSpec)) {
                QTextEdit::ExtraSelection s;
                s.cursor = QTextCursor(b);
                s.cursor.setPosition(b.position() + r.first);
                s.cursor.setPosition(b.position() + r.first + r.second, QTextCursor::KeepAnchor);
                s.format.setBackground(st.foundBgColor);
                sels << s;
            }
        }
    }
    if (!m_searchText.isEmpty()) {
        QTextCursor c(document());
        int count = 0;
        while (count++ < 2000) {
            c = document()->find(m_searchText, c);
            if (c.isNull()) break;
            QTextEdit::ExtraSelection s;
            s.cursor = c;
            bool current = !m_currentSearchHit.isNull() && c.selectionStart() == m_currentSearchHit.selectionStart();
            s.format.setBackground(current ? st.currentSearchBgColor : st.foundBgColor);
            sels << s;
        }
    }
    setExtraSelections(sels);
}

void DiffView::mouseMoveEvent(QMouseEvent *e) {
    QPlainTextEdit::mouseMoveEvent(e);
    viewport()->setCursor(anchorAt(e->pos()).isEmpty() ? Qt::IBeamCursor : Qt::PointingHandCursor);
}

void DiffView::mouseReleaseEvent(QMouseEvent *e) {
    QPlainTextEdit::mouseReleaseEvent(e);
    if (e->button() != Qt::LeftButton || textCursor().hasSelection()) return;
    QString href = anchorAt(e->pos());
    if (href.startsWith(QLatin1String("commit:"))) {
        emit commitLinkActivated(Oid::fromHex(href.mid(7)));
    } else if (href.startsWith(QLatin1String("copy:"))) {
        QApplication::clipboard()->setText(href.mid(5));
        if (QApplication::clipboard()->supportsSelection()) QApplication::clipboard()->setText(href.mid(5), QClipboard::Selection);
        emit statusMessage(tr("Copied commit ID %1").arg(href.mid(5)), 3000);
    } else if (href.startsWith(QLatin1String("subtoggle:"))) {
        emit submoduleToggled(QUrl::fromPercentEncoding(href.mid(10).toLatin1()));
    } else if (href.startsWith(QLatin1String("subcommit:"))) {
        const QString rest = href.mid(10);
        const int colon = int(rest.indexOf(QLatin1Char(':')));
        emit submoduleOpenRequested(QUrl::fromPercentEncoding(rest.mid(colon + 1).toLatin1()),
                                    colon > 0 ? Oid::fromHex(rest.left(colon)) : Oid());
    }
}

void DiffView::contextMenuEvent(QContextMenuEvent *e) {
    emit diffContextMenu(lineInfoAt(e->pos()), e->globalPos());
}

} // namespace gitq

#include "DiffView.moc"
