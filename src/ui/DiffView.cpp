#include "ui/DiffView.h"

#include "Settings.h"
#include "model/CommitModel.h"
#include "ui/Glyphs.h"
#include "ui/Syntax.h"

#include <QApplication>
#include <cmath>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QStyleOptionSlider>
#include <QTimer>
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

// The vertical scroll bar, with marks showing where the changes, files and search hits are.
class MarkerScrollBar : public QScrollBar {
public:
    explicit MarkerScrollBar(DiffView *view) : QScrollBar(Qt::Vertical, view), m_view(view) {}

protected:
    void paintEvent(QPaintEvent *e) override {
        QScrollBar::paintEvent(e);
        if (m_view->m_markers.isEmpty()) return;
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        QRect groove = style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarGroove, this);
        if (groove.height() <= 0) groove = rect();
        QPainter p(this);
        // A strip along the right edge, so the slider stays visible beside it.
        const double h = groove.height();
        const double strip = qMax(3.0, groove.width() * 0.4);
        const double x = groove.right() + 1 - strip - 1;
        for (const DiffView::Marker &m : std::as_const(m_view->m_markers)) {
            const double y1 = groove.top() + m.from * h, y2 = qMax(y1 + 2, groove.top() + m.to * h);
            p.fillRect(QRectF(x, y1, strip, y2 - y1), m.color);
        }
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
        if (style == DiffView::SplitStyle) {
            highlightSplit(n, text);
            return;
        }
        const int marks = n < m_view->m_markLen.size() ? m_view->m_markLen[n] : 0;
        highlightSyntax(n, text, style, marks);
        switch (style) {
        case DiffView::OldStyle:
        case DiffView::NewStyle: {
            // The text keeps its colour on the tinted line; the +/- marks take the line's colour.
            QTextCharFormat mark = fg(style == DiffView::OldStyle ? st.diffOldColor : st.diffNewColor);
            mark.setFontWeight(QFont::Bold);
            setFormat(0, qMin(marks, int(text.size())), mark);
            break;
        }
        case DiffView::HunkStyle: setFormat(0, int(text.size()), fg(st.diffHunkColor)); break;
        case DiffView::MetaStyle: setFormat(0, int(text.size()), fg(Settings::mix(st.fgColor, st.bgColor, 0.5))); break;
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
        if (m_view->m_wordMode == DiffView::LineDiff) {
            // Changed words of a changed line: a stronger tint of the line's background, keeping
            // the syntax colours.
            for (const auto &r : *it) {
                const QColor bg = DiffView::wordBg(r.kind == DiffView::OldStyle);
                for (int i = r.start; i < r.start + r.len && i < text.size(); ++i) {
                    QTextCharFormat f = format(i);
                    f.setBackground(bg);
                    setFormat(i, 1, f);
                }
            }
            return;
        }
        for (const auto &r : *it) {
            QTextCharFormat f = fg(r.kind == DiffView::OldStyle ? st.diffOldColor : st.diffNewColor);
            if (colorWords) f.setBackground(r.kind == DiffView::OldStyle ? st.diffOldBg : st.diffNewBg);
            setFormat(r.start, r.len, f);
        }
    }

private:
    // Syntax colours of a code line. The block state follows constructs spanning lines (block
    // comments, triple-quoted strings) separately for the old and new sides: removed lines continue
    // the old side's state, added lines the new side's, context lines both.
    void highlightSyntax(int n, const QString &text, quint8 style, int marks) {
        setCurrentBlockState(0);
        if (n >= m_view->m_meta.size() || m_view->m_wordMode != DiffView::LineDiff ||
            !Settings::instance().syntaxHighlighting)
            return;
        const DiffLineInfo &meta = m_view->m_meta[n];
        if (meta.kind.isNull() || (style != DiffView::OldStyle && style != DiffView::NewStyle && style != DiffView::PlainStyle))
            return;
        auto it = m_languages.constFind(meta.path);
        if (it == m_languages.cend()) it = m_languages.insert(meta.path, Syntax::languageFor(meta.path));
        if (!*it) return;
        int prev = previousBlockState();
        if (prev < 0) prev = 0;
        int oldState = prev & 0xf, newState = (prev >> 4) & 0xf;
        QList<Syntax::Span> spans;
        if (meta.kind == QLatin1Char('-')) {
            oldState = Syntax::highlightLine(**it, text, marks, oldState, &spans);
        } else if (meta.kind == QLatin1Char('+')) {
            newState = Syntax::highlightLine(**it, text, marks, newState, &spans);
        } else {
            oldState = newState = Syntax::highlightLine(**it, text, marks, newState, &spans);
        }
        setCurrentBlockState(oldState | (newState << 4));
        QTextCharFormat colors[Syntax::TokenCount];
        for (int t = 0; t < Syntax::TokenCount; ++t) colors[t].setForeground(Syntax::color(Syntax::Token(t)));
        colors[Syntax::Comment].setFontItalic(true);
        for (const Syntax::Span &s : std::as_const(spans)) setFormat(s.start, s.len, colors[s.token]);
    }

    const Syntax::Language *language(const QString &path) {
        auto it = m_languages.constFind(path);
        if (it == m_languages.cend()) it = m_languages.insert(path, Syntax::languageFor(path));
        return *it;
    }

    // A side-by-side row: each side highlighted on its own (following that side's state), then
    // the changed words' tint.
    void highlightSplit(int n, const QString &text) {
        const DiffView::SplitRow row = m_view->m_splitRows.value(n);
        const int w = m_view->m_splitWidth, right = w + DiffView::kSplitMiddle;
        int prev = previousBlockState();
        if (prev < 0) prev = 0;
        int oldState = prev & 0xf, newState = (prev >> 4) & 0xf;
        const Syntax::Language *lang = Settings::instance().syntaxHighlighting ? language(m_view->m_meta[n].path) : nullptr;
        if (lang) {
            QTextCharFormat colors[Syntax::TokenCount];
            for (int t = 0; t < Syntax::TokenCount; ++t) colors[t].setForeground(Syntax::color(Syntax::Token(t)));
            colors[Syntax::Comment].setFontItalic(true);
            auto side = [&](const QString &part, int shift, int state) {
                QList<Syntax::Span> spans;
                const int s = Syntax::highlightLine(*lang, part, 0, state, &spans);
                for (const Syntax::Span &sp : std::as_const(spans)) setFormat(sp.start + shift, sp.len, colors[sp.token]);
                return s;
            };
            if (!row.left.isNull()) oldState = side(text.left(w), 0, oldState);
            if (!row.right.isNull()) newState = side(text.mid(right), right, newState);
        }
        setCurrentBlockState(oldState | (newState << 4));
        for (const auto &r : m_view->m_wordRanges.value(n)) {
            const QColor bg = DiffView::wordBg(r.kind == DiffView::OldStyle);
            for (int i = r.start; i < r.start + r.len && i < text.size(); ++i) {
                QTextCharFormat f = format(i);
                f.setBackground(bg);
                setFormat(i, 1, f);
            }
        }
    }

    DiffView *m_view;
    QHash<QString, const Syntax::Language *> m_languages;
};

DiffView::DiffView(QWidget *parent) : QPlainTextEdit(parent) {
    document()->setDocumentLayout(new CenteredObjectLayout(document()));
    document()->documentLayout()->registerHandler(kObjectType, new HeaderObjects(this));
    new DiffHighlighter(this);
    setVerticalScrollBar(new MarkerScrollBar(this));
    m_gutter = new LineGutter(this);
    m_resplitTimer = new QTimer(this);
    m_resplitTimer->setSingleShot(true);
    m_resplitTimer->setInterval(150);
    connect(m_resplitTimer, &QTimer::timeout, this, [this] {
        if (m_splitWidth > 0 && splitColumns() != m_splitWidth) rerenderBody();
    });
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
    // Side-by-side rows are wrapped by the view itself, to the column width.
    setLineWrapMode(st.wrapDiff && !m_sideBySide ? WidgetWidth : NoWrap);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, st.bgColor);
    pal.setColor(QPalette::Text, st.fgColor);
    setPalette(pal);
}

void DiffView::clearAll() {
    clear();
    m_meta.clear();
    m_style.clear();
    m_markLen.clear();
    m_wordRanges.clear();
    m_pending.clear();
    m_fileStarts.clear();
    m_hunkActions.clear();
    m_pressedButton.reset();
    m_hoverButton = {};
    m_headerEnd = 0;
    m_cardEnd = 0;
    m_avatarText.clear();
    m_cardPills.clear();
    m_cardId.clear();
    m_singleNumberColumn = false;
    m_splitRows.clear();
    m_splitWidth = 0;
    m_hasResult = false;
    m_markers.clear();
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
    m_markLen.append(0);
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
        m_markLen.insert(at, 0);
    }
    QHash<int, QList<WordRange>> words;
    for (auto it = m_wordRanges.cbegin(); it != m_wordRanges.cend(); ++it) words.insert(it.key() + k, it.value());
    m_wordRanges = words;
    QMap<int, FileHeader> shifted;
    for (auto it = m_fileStarts.cbegin(); it != m_fileStarts.cend(); ++it) shifted.insert(it.key() + k, it.value());
    m_fileStarts = shifted;
    QHash<int, SplitRow> rows;
    for (auto it = m_splitRows.cbegin(); it != m_splitRows.cend(); ++it) rows.insert(it.key() + k, it.value());
    m_splitRows = rows;
    m_headerEnd += k;
    m_cardEnd += k;
    m_nearbyBlock = -1;
    verticalScrollBar()->setValue(keep);
    updateExtraSelections();
}

QColor DiffView::wordBg(bool old) {
    const Settings &st = Settings::instance();
    return old ? Settings::mix(st.diffOldBg, st.diffOldColor, 0.22) : Settings::mix(st.diffNewBg, st.diffNewColor, 0.22);
}

int DiffView::gutterWidth() const {
    if (m_gutterDigits == 0) return 0;
    const int digit = QFontMetrics(Settings::instance().textFont).horizontalAdvance(QLatin1Char('9'));
    const int column = m_gutterDigits * digit + 10;
    return (m_singleNumberColumn || m_splitWidth > 0 ? column : 2 * column) + 6;
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
    if (m_splitWidth > 0) m_resplitTimer->start();
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
        if (n < m_style.size() && (m_style[n] == OldStyle || m_style[n] == NewStyle))
            p.fillRect(QRectF(0, r.top(), w - 1, r.height()), wordBg(m_style[n] == OldStyle));
        if (auto split = m_splitRows.constFind(n); split != m_splitRows.cend()) {
            // Side by side: the old side's numbers here, the new side's between the columns.
            if (split->left == QLatin1Char('-')) p.fillRect(QRectF(0, r.top(), w - 1, r.height()), wordBg(true));
            if (m_meta[n].oldLine > 0) {
                p.setPen(number);
                p.drawText(QRectF(0, r.top(), column - 4, QFontMetrics(font).height()), Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(m_meta[n].oldLine));
            }
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
    {
        // Tinted backgrounds of removed, added and hunk header lines.
        QPainter p(viewport());
        const QPointF offset = contentOffset();
        const QColor hunkBg = Settings::mix(st.bgColor, st.diffHunkColor, Settings::darkTheme() ? 0.12 : 0.06);
        for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
            const QRectF r = blockBoundingGeometry(b).translated(offset);
            if (r.top() > e->rect().bottom()) break;
            const int n = b.blockNumber();
            if (n >= m_style.size()) break;
            const quint8 style = m_style[n];
            if (style == SplitStyle) {
                const SplitRow row = m_splitRows.value(n);
                const double mid = splitRightX();
                const double cw = QFontMetricsF(st.textFont).horizontalAdvance(QLatin1Char(' '));
                const double leftEnd = mid - (kSplitMiddle - 1) * cw;
                const QBrush filler(Settings::mix(st.bgColor, st.fgColor, 0.12), Qt::BDiagPattern);
                auto side = [&](QChar kind, double x1, double x2, bool old) {
                    const QRectF rr(x1, r.top(), x2 - x1, r.height());
                    if (kind.isNull()) p.fillRect(rr, filler);
                    else if (kind != QLatin1Char(' ')) p.fillRect(rr, old ? st.diffOldBg : st.diffNewBg);
                };
                side(row.left, 0, leftEnd, true);
                side(row.right, mid, viewport()->width(), false);
                if (row.right == QLatin1Char('+')) p.fillRect(QRectF(leftEnd, r.top(), mid - leftEnd, r.height()), wordBg(false));
                continue;
            }
            const QColor bg = style == OldStyle ? st.diffOldBg : style == NewStyle ? st.diffNewBg : style == HunkStyle ? hunkBg : QColor();
            if (bg.isValid()) p.fillRect(QRectF(0, r.top(), viewport()->width(), r.height()), bg);
        }
    }
    QPlainTextEdit::paintEvent(e);
    if (!m_hunkActions.isEmpty()) {
        // Buttons at the right end of hunk lines, over a patch of the line's background.
        QPainter p(viewport());
        const QPointF offset = contentOffset();
        const QColor hunkBg = Settings::mix(st.bgColor, st.diffHunkColor, Settings::darkTheme() ? 0.12 : 0.06);
        for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
            const QRectF r = blockBoundingGeometry(b).translated(offset);
            if (r.top() > e->rect().bottom()) break;
            const int n = b.blockNumber();
            auto it = m_hunkActions.constFind(n);
            if (it == m_hunkActions.cend()) continue;
            const QList<Button> buttons = hunkButtons(r, m_meta[n].path, m_meta[n].hunk, it.value());
            if (buttons.isEmpty()) continue;
            p.fillRect(QRectF(buttons.last().rect.left() - 8, r.top(), r.right() - buttons.last().rect.left() + 8, r.height()), hunkBg);
            for (const Button &bt : buttons) paintButton(&p, bt, hunkBg);
        }
    }
    if (m_splitWidth > 0 && !m_splitRows.isEmpty()) {
        // Side by side: the divider and the new side's line numbers between the columns.
        QPainter p(viewport());
        const QPointF offset = contentOffset();
        const double cw = QFontMetricsF(st.textFont).horizontalAdvance(QLatin1Char(' '));
        const double mid = splitRightX(), leftEnd = mid - (kSplitMiddle - 1) * cw;
        p.setFont(st.textFont);
        const QColor number = Settings::mix(st.fgColor, st.bgColor, 0.55);
        const QColor divider = Settings::mix(st.bgColor, st.fgColor, 0.15);
        for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
            const QRectF r = blockBoundingGeometry(b).translated(offset);
            if (r.top() > e->rect().bottom()) break;
            const int n = b.blockNumber();
            if (!m_splitRows.contains(n)) continue;
            p.setPen(divider);
            p.drawLine(QPointF(leftEnd + 0.5, r.top()), QPointF(leftEnd + 0.5, r.bottom() + 1));
            if (m_meta[n].newLine > 0) {
                p.setPen(number);
                p.drawText(QRectF(leftEnd, r.top(), mid - leftEnd - cw, r.height()), Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(m_meta[n].newLine));
            }
        }
    }
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
        const double s = qMin(r.height() - 6, fm.height() + 2);
        p->setFont(font);
        drawStatusBadge(p, QRectF(x, cy - s / 2, s, s), h.status);
        x += s + 8;
    }

    // Right side: buttons, then line counts and a five-block bar, or "binary".
    double right = r.right() - 10;
    const QList<Button> buttons = headerButtons(r, h);
    for (const Button &b : buttons) {
        paintButton(p, b, bg);
        right = qMin(right, b.rect.left() - 14);
    }
    p->setFont(font);
    if (h.binary) {
        const QString t = tr("binary");
        right -= fm.horizontalAdvance(t);
        p->setPen(dim);
        p->drawText(QRectF(right, r.top(), fm.horizontalAdvance(t) + 1, r.height()), Qt::AlignVCenter, t);
        right -= 10;
    } else if (h.added || h.removed) {
        const QColor plus = addedColor(), minus = removedColor();
        if (!h.submodule) right = drawChangeBar(p, right, cy, qMax(5.0, fm.height() * 0.5), h.added, h.removed) - 8;
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
    if (e->button() == Qt::LeftButton) {
        if (auto b = buttonAt(e->position())) {
            m_pressedButton = b;
            e->accept();
            return;
        }
    }
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
        if (auto b = buttonAt(he->pos())) {
            QString tip;
            switch (b->action) {
            case StageAction: tip = b->hunk >= 0 ? tr("Add this hunk to the index") : tr("Add the file's changes to the index"); break;
            case UnstageAction: tip = b->hunk >= 0 ? tr("Take this hunk out of the index") : tr("Take the file's changes out of the index"); break;
            case DiscardAction: tip = b->hunk >= 0 ? tr("Revert this hunk in the work tree") : tr("Revert the file in the work tree to the index"); break;
            case DeleteAction: tip = tr("Delete this untracked file"); break;
            }
            QToolTip::showText(he->globalPos(), tip, viewport());
            return true;
        }
        QToolTip::hideText();
    }
    return QPlainTextEdit::viewportEvent(e);
}

void DiffView::setSideBySide(bool on) {
    m_sideBySide = on;
    applyFonts();
}

int DiffView::splitColumns() const {
    const double cw = QFontMetricsF(Settings::instance().textFont).horizontalAdvance(QLatin1Char(' '));
    const double avail = viewport()->width() - 2 * document()->documentMargin() - 4;
    const int total = int(avail / qMax(1.0, cw)) - 1;
    return qMax(16, (total - kSplitMiddle) / 2);
}

double DiffView::splitRightX() const {
    const double cw = QFontMetricsF(Settings::instance().textFont).horizontalAdvance(QLatin1Char(' '));
    return contentOffset().x() + document()->documentMargin() + (m_splitWidth + kSplitMiddle) * cw - cw / 2;
}

void DiffView::rerenderBody() {
    if (!m_hasResult) return;
    const int keep = verticalScrollBar()->value();
    QTextCursor c(document());
    if (m_headerEnd > 0) {
        c.setPosition(document()->findBlockByNumber(m_headerEnd - 1).position());
        c.movePosition(QTextCursor::EndOfBlock);
    }
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    m_meta.resize(m_headerEnd);
    m_style.resize(m_headerEnd);
    m_markLen.resize(m_headerEnd);
    for (auto it = m_wordRanges.begin(); it != m_wordRanges.end();) it = it.key() >= m_headerEnd ? m_wordRanges.erase(it) : std::next(it);
    m_fileStarts.erase(m_fileStarts.lowerBound(m_headerEnd), m_fileStarts.end());
    for (auto it = m_hunkActions.begin(); it != m_hunkActions.end();) it = it.key() >= m_headerEnd ? m_hunkActions.erase(it) : std::next(it);
    m_splitRows.clear();
    const DiffResult diff = m_lastResult;
    appendDiff(diff);
    verticalScrollBar()->setValue(keep);
}

void DiffView::appendDiff(const DiffResult &diff) {
    const int keep = verticalScrollBar()->value();
    m_lastResult = diff;
    m_hasResult = true;
    m_splitWidth = m_sideBySide && m_mode == ShowDiff ? splitColumns() : 0;
    if (!diff.error.isEmpty()) addBodyLine(diff.error, ErrorStyle, {});
    for (const FileDiff &f : diff.files) {
        if (f.submodule >= 0 && f.submodule < diff.submodules.size() && diff.submodules[f.submodule].detailed) {
            const int start = int(m_meta.size());
            renderSubmodule(diff.submodules[f.submodule]);
            if (m_fileStarts.contains(start)) m_fileStarts[start].actions = fileActionsFor(f);
        } else
            renderFile(f);
    }
    flushBody();
    updateGutter();
    // The gutter's width may have changed the room for the columns.
    if (m_splitWidth > 0 && !m_splitRows.isEmpty() && splitColumns() != m_splitWidth) {
        rerenderBody();
        return;
    }
    if (m_splitRows.isEmpty()) m_splitWidth = 0;
    verticalScrollBar()->setValue(keep);
    updateExtraSelections();
    onScroll();
}

void DiffView::addBodyLine(const QString &text, quint8 style, const DiffLineInfo &meta, const QList<WordRange> &words,
                           int markLen) {
    if (!words.isEmpty()) m_wordRanges.insert(int(m_meta.size()), words);
    if (style == HunkStyle && m_curHunkActions) m_hunkActions.insert(int(m_meta.size()), m_curHunkActions);
    m_pending << text;
    m_meta.append(meta);
    if (m_curHunk >= 0 && m_meta.last().hunk < 0) m_meta.last().hunk = m_curHunk;
    m_style.append(style);
    m_markLen.append(quint8(markLen));
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

QList<QPair<int, int>> DiffView::pairSimilarLines(const QStringList &olds, const QStringList &news) {
    // Each old line goes with the most similar new line after the previous pair's (looking a few
    // lines ahead), if they are similar enough.
    QList<QPair<int, int>> pairs;
    int from = 0;
    for (int i = 0; i < olds.size() && from < news.size(); ++i) {
        double best = 0;
        int bestAt = -1;
        for (int k = from; k < qMin(int(news.size()), from + 8); ++k) {
            QList<WordRange> o, n;
            const double sim = changedWords(olds[i], news[k], 0, &o, &n);
            if (sim > best) {
                best = sim;
                bestAt = k;
            }
        }
        if (bestAt < 0 || best < 0.4) continue;
        pairs.append({i, bestAt});
        from = bestAt + 1;
    }
    return pairs;
}

double DiffView::changedWords(const QString &oldText, const QString &newText, int offset, QList<WordRange> *oldRanges,
                              QList<WordRange> *newRanges) {
    if (oldText.size() > 1000 || newText.size() > 1000) return 0;
    const QList<WordSeg> segs = DiffEngine::wordDiff(oldText, newText);
    int same = 0, op = offset, np = offset;
    QList<WordRange> o, nw;
    for (const WordSeg &s : segs) {
        const int len = int(s.text.size());
        switch (s.kind) {
        case WordSeg::Same:
            same += int(s.text.trimmed().size());
            op += len;
            np += len;
            break;
        case WordSeg::Removed:
            o.append({op, len, OldStyle});
            op += len;
            break;
        case WordSeg::Added:
            nw.append({np, len, NewStyle});
            np += len;
            break;
        }
    }
    const int total = int(oldText.trimmed().size() + newText.trimmed().size());
    if (total == 0) return 0;
    *oldRanges = o;
    *newRanges = nw;
    return 2.0 * same / total;
}

void DiffView::renderSplitFile(const FileDiff &f, const DiffLineInfo &fileMeta) {
    const QString path = f.path();
    const int tab = qMax(1, Settings::instance().tabStop);
    auto expand = [tab](const QString &s) {
        if (!s.contains(QLatin1Char('\t'))) return s;
        QString out;
        for (QChar c : s) {
            if (c == QLatin1Char('\t')) out += QString(tab - out.size() % tab, QLatin1Char(' '));
            else out += c;
        }
        return out;
    };
    bool inHunks = false;
    int oldNo = 0, newNo = 0;
    QList<SideLine> dels, adds;
    // A change: similar lines side by side; the lines between them side by side as they come.
    auto flush = [&] {
        QStringList olds, news;
        for (const SideLine &l : std::as_const(dels)) olds << l.text;
        for (const SideLine &l : std::as_const(adds)) news << l.text;
        QList<QPair<int, int>> pairs = pairSimilarLines(olds, news);
        pairs.append({int(dels.size()), int(adds.size())});   // the end
        int di = 0, ai = 0;
        for (const auto &[i, k] : std::as_const(pairs)) {
            while (di < i || ai < k) {
                addSplitRows(path, di < i ? dels[di] : SideLine(), ai < k ? adds[ai] : SideLine());
                if (di < i) ++di;
                if (ai < k) ++ai;
            }
            if (i < dels.size() && k < adds.size()) addSplitRows(path, dels[i], adds[k]);
            di = i + 1;
            ai = k + 1;
        }
        dels.clear();
        adds.clear();
    };
    for (const QString &line : f.lines) {
        if (!inHunks) {
            if (!line.startsWith(QLatin1String("@@"))) {
                if (!line.startsWith(QLatin1String("diff --")) && !line.startsWith(QLatin1String("--- ")) &&
                    !line.startsWith(QLatin1String("+++ ")))
                    addBodyLine(line, MetaStyle, fileMeta);
                continue;
            }
            inHunks = true;
        }
        if (line.startsWith(QLatin1String("@@"))) {
            flush();
            ++m_curHunk;
            auto m = hunkRe().match(line);
            if (m.hasMatch()) {
                oldNo = m.captured(1).toInt();
                newNo = m.captured(2).toInt();
            }
            addBodyLine(line, HunkStyle, fileMeta);
            continue;
        }
        if (line.startsWith(QLatin1Char('\\'))) {
            flush();
            addBodyLine(line, MetaStyle, fileMeta);
            continue;
        }
        const QChar kind = line.isEmpty() ? QLatin1Char(' ') : line[0];
        const QString text = expand(line.mid(1));
        if (kind == QLatin1Char('-')) {
            if (!adds.isEmpty()) flush();
            dels.append({kind, text, oldNo++});
        } else if (kind == QLatin1Char('+')) {
            adds.append({kind, text, newNo++});
        } else {
            flush();
            addSplitRows(path, {kind, text, oldNo++}, {kind, text, newNo++});
        }
    }
    flush();
}

void DiffView::addSplitRows(const QString &path, const SideLine &left, const SideLine &right) {
    const int w = m_splitWidth;
    QList<WordRange> lw, rw;
    if (left.kind == QLatin1Char('-') && right.kind == QLatin1Char('+') && changedWords(left.text, right.text, 0, &lw, &rw) < 0.4) {
        lw.clear();
        rw.clear();
    }
    auto chunks = [w](const SideLine &l) {
        QStringList out;
        if (!l.kind.isNull())
            for (int i = 0; i < l.text.size(); i += w) out << l.text.mid(i, w);
        if (out.isEmpty()) out << QString();
        return out;
    };
    const QStringList lc = chunks(left), rc = chunks(right);
    const int rows = int(qMax(lc.size(), rc.size()));
    for (int r = 0; r < rows; ++r) {
        const QString text = lc.value(r).leftJustified(w, QLatin1Char(' ')) + QString(kSplitMiddle, QLatin1Char(' ')) + rc.value(r);
        // Word ranges of this piece of each side.
        QList<WordRange> ranges;
        auto clip = [&](const QList<WordRange> &list, int shift, quint8 kind) {
            for (const WordRange &wr : list) {
                const int s = qMax(wr.start, r * w), e = qMin(wr.start + wr.len, (r + 1) * w);
                if (e > s) ranges.append({s - r * w + shift, e - s, kind});
            }
        };
        clip(lw, 0, OldStyle);
        clip(rw, w + kSplitMiddle, NewStyle);
        DiffLineInfo meta;
        meta.path = path;
        meta.kind = left.kind == QLatin1Char('-') ? QLatin1Char('-') : right.kind == QLatin1Char('+') ? QLatin1Char('+') : QLatin1Char(' ');
        if (r == 0) {
            meta.oldLine = left.kind.isNull() ? 0 : left.line;
            meta.newLine = right.kind.isNull() ? 0 : right.line;
        }
        m_splitRows.insert(int(m_meta.size()), {left.kind, right.kind});
        addBodyLine(text, SplitStyle, meta, ranges, 0);
    }
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
    header.actions = fileActionsFor(f);
    f.countChanges(&header.added, &header.removed);
    m_fileStarts.insert(int(m_meta.size()), header);
    DiffLineInfo fileMeta;
    fileMeta.path = f.path();
    addBodyLine(header.title, FileSepStyle, fileMeta);
    // Body lines are tagged with their hunk (counted from 0 at the first "@@").
    m_curHunk = -1;
    m_curHunkActions = hunkActionsFor(f);
    struct EndFile {
        DiffView *v;
        ~EndFile() {
            v->m_curHunk = -1;
            v->m_curHunkActions = 0;
        }
    } endFile{this};
    if (m_splitWidth > 0 && n == 1) {
        renderSplitFile(f, fileMeta);
        return;
    }

    bool inHunks = false;
    int oldNo = 0, newNo = 0;
    QStringList dels, adds;
    const bool words = m_wordMode != LineDiff && n == 1 && m_mode == ShowDiff;
    // Line diff: a run of removed lines followed by added ones is emitted together, so that each
    // removed line and the added line paired with it can show which words changed.
    const bool pairLines = m_wordMode == LineDiff && n == 1 && m_mode == ShowDiff;
    struct RunLine {
        QString text;
        DiffLineInfo meta;
    };
    QList<RunLine> lineDels, lineAdds;
    auto flushLines = [&] {
        QList<QList<WordRange>> delWords(lineDels.size()), addWords(lineAdds.size());
        QStringList olds, news;
        for (const RunLine &l : std::as_const(lineDels)) olds << l.text.mid(1);
        for (const RunLine &l : std::as_const(lineAdds)) news << l.text.mid(1);
        for (const auto &[i, k] : pairSimilarLines(olds, news))
            changedWords(olds[i], news[k], 1, &delWords[i], &addWords[k]);
        for (int i = 0; i < lineDels.size(); ++i) addBodyLine(lineDels[i].text, OldStyle, lineDels[i].meta, delWords[i], 1);
        for (int i = 0; i < lineAdds.size(); ++i) addBodyLine(lineAdds[i].text, NewStyle, lineAdds[i].meta, addWords[i], 1);
        lineDels.clear();
        lineAdds.clear();
    };
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
                addBodyLine(line, MetaStyle, fileMeta);
                continue;
            }
        }
        if (line.startsWith(QLatin1String("@@"))) {
            flushRun();
            flushLines();
            ++m_curHunk;
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
            flushLines();
            addBodyLine(line, MetaStyle, fileMeta);
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
        if (pairLines && (isDel || isAdd)) {
            if (isDel && !lineAdds.isEmpty()) flushLines();
            (isDel ? lineDels : lineAdds).append({line, meta});
            continue;
        }
        flushLines();
        const bool diff = m_mode == ShowDiff;
        QString text = diff ? line : line.mid(n);
        addBodyLine(text, isDel ? OldStyle : isAdd ? NewStyle : PlainStyle, meta, {}, diff ? n : 0);
    }
    flushRun();
    flushLines();
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

int DiffView::hunkBlock(const QString &path, int hunk) const {
    for (int i = 0; i < m_meta.size(); ++i)
        if (m_style[i] == HunkStyle && m_meta[i].hunk == hunk && m_meta[i].path == path) return i;
    return -1;
}

int DiffView::hunkCount(const QString &path) const {
    int n = 0;
    for (int i = 0; i < m_meta.size(); ++i)
        if (m_style[i] == HunkStyle && m_meta[i].path == path) n = qMax(n, m_meta[i].hunk + 1);
    return n;
}

std::optional<int> DiffView::hunkLineOffset(const QString &path, int hunk) const {
    const int b = hunkBlock(path, hunk);
    if (b < 0) return std::nullopt;
    return document()->findBlockByNumber(b).firstLineNumber() - verticalScrollBar()->value();
}

bool DiffView::scrollToHunk(const QString &path, int hunk, int lineOffset) {
    const int b = hunkBlock(path, hunk);
    if (b < 0) return false;
    const QTextBlock block = document()->findBlockByNumber(b);
    QTextCursor c(block);
    setTextCursor(c);
    verticalScrollBar()->setValue(block.firstLineNumber() - lineOffset);
    return true;
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
    if (b < 0 || b >= m_meta.size()) return {};
    DiffLineInfo info = m_meta[b];
    auto split = m_splitRows.constFind(b);
    if (split == m_splitRows.cend()) return info;
    // Side by side: the line on the side clicked.
    const bool right = pos.x() >= splitRightX() - QFontMetricsF(Settings::instance().textFont).horizontalAdvance(QLatin1Char(' ')) * kSplitMiddle / 2;
    info.kind = right ? split->right : split->left;
    if (info.kind.isNull()) return {};
    if (right && info.kind != QLatin1Char(' ')) info.oldLine = 0;
    if (!right && info.kind != QLatin1Char(' ')) info.newLine = 0;
    return info;
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

namespace {
// Text colour for a highlight: the syntax colours can be unreadable on it (red on olive, say).
QColor textOn(const QColor &bg) {
    auto lin = [](double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const double l = 0.2126 * lin(bg.redF()) + 0.7152 * lin(bg.greenF()) + 0.0722 * lin(bg.blueF());
    return l > 0.18 ? QColor(Qt::black) : QColor(Qt::white);   // whichever contrasts more
}
} // namespace

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
                s.format.setForeground(textOn(st.foundBgColor));
                sels << s;
            }
        }
    }
    QList<int> hits;
    if (!m_searchText.isEmpty()) {
        QTextCursor c(document());
        int count = 0;
        while (count++ < 2000) {
            c = document()->find(m_searchText, c);
            if (c.isNull()) break;
            hits << c.blockNumber();
            QTextEdit::ExtraSelection s;
            s.cursor = c;
            bool current = !m_currentSearchHit.isNull() && c.selectionStart() == m_currentSearchHit.selectionStart();
            const QColor bg = current ? st.currentSearchBgColor : st.foundBgColor;
            s.format.setBackground(bg);
            s.format.setForeground(textOn(bg));
            sels << s;
        }
    }
    setExtraSelections(sels);
    updateMarkers(hits);
}

void DiffView::updateMarkers(const QList<int> &searchHits) {
    const Settings &st = Settings::instance();
    m_markers.clear();
    const double total = qMax(1, document()->blockCount());
    // Runs of changed lines, merged when close together.
    auto add = [&](int from, int to, const QColor &c) {
        if (!m_markers.isEmpty() && m_markers.last().color == c && from / total - m_markers.last().to < 0.004) {
            m_markers.last().to = to / total;
            return;
        }
        m_markers.append({from / total, to / total, c});
    };
    const QColor removed = st.diffOldColor, added = st.diffNewColor;
    for (int n = m_headerEnd; n < m_style.size(); ++n) {
        QColor c;
        if (m_style[n] == OldStyle) c = removed;
        else if (m_style[n] == NewStyle) c = added;
        else if (m_style[n] == SplitStyle) {
            const SplitRow row = m_splitRows.value(n);
            c = row.right == QLatin1Char('+') ? added : row.left == QLatin1Char('-') ? removed : QColor();
        }
        if (c.isValid()) add(n, n + 1, c);
    }
    const QColor file = Settings::mix(st.fgColor, st.bgColor, 0.55);
    for (auto it = m_fileStarts.cbegin(); it != m_fileStarts.cend(); ++it) m_markers.append({it.key() / total, it.key() / total, file});
    for (int b : searchHits) m_markers.append({b / total, (b + 1) / total, st.currentSearchBgColor});
    verticalScrollBar()->update();
}

// ---- Action buttons ---------------------------------------------------------------------------

int DiffView::fileActionsFor(const FileDiff &f) const {
    if (m_actionSet == NoActions || !f.inSubmodule.isEmpty() || f.numParents > 1) return 0;
    if (m_actionSet == IndexActions) return UnstageAction;
    if (f.submodule >= 0 || f.status == QLatin1Char('U')) return StageAction;
    if (f.status == QLatin1Char('?')) return StageAction | DeleteAction;
    return StageAction | DiscardAction;
}

int DiffView::hunkActionsFor(const FileDiff &f) const {
    if (!m_hunkActionsOn || m_mode != ShowDiff || !fileActionsFor(f) || f.binary || f.submodule >= 0) return 0;
    const QChar s = f.status;
    if (s != QLatin1Char('M') && s != QLatin1Char('A') && s != QLatin1Char('D')) return 0;
    return m_actionSet == IndexActions ? int(UnstageAction) : StageAction | DiscardAction;
}

int DiffView::fileActions(const QString &path) const {
    for (const FileDiff &f : m_lastResult.files)
        if (f.path() == path && f.inSubmodule.isEmpty()) return fileActionsFor(f);
    return 0;
}

int DiffView::hunkActions(const QString &path) const {
    for (const FileDiff &f : m_lastResult.files)
        if (f.path() == path && f.inSubmodule.isEmpty()) return hunkActionsFor(f);
    return 0;
}

QString DiffView::actionLabel(int action, bool hunk) {
    switch (action) {
    case StageAction: return hunk ? tr("Stage hunk") : tr("Stage");
    case UnstageAction: return hunk ? tr("Unstage hunk") : tr("Unstage");
    case DiscardAction: return hunk ? tr("Discard hunk") : tr("Discard");
    case DeleteAction: return tr("Delete");
    }
    return {};
}

namespace {

QFont buttonFont() {
    QFont f = Settings::instance().uiFont;
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 0.9);
    else f.setPixelSize(qMax(8, int(f.pixelSize() * 0.9)));
    return f;
}

// Buttons laid out from the right edge `right` leftwards, the primary action (stage / unstage) last
// in the list and rightmost; returned right to left.
QList<QPair<int, QRectF>> layoutButtons(int actions, bool hunk, double right, double cy, double maxH) {
    const QFontMetricsF fm(buttonFont());
    const double h = qMin(maxH, fm.height() + 6);
    QList<QPair<int, QRectF>> out;
    for (int a : {int(DiffView::StageAction), int(DiffView::UnstageAction), int(DiffView::DiscardAction),
                  int(DiffView::DeleteAction)}) {
        if (!(actions & a)) continue;
        const QString label = DiffView::actionLabel(a, hunk);
        const double w = fm.horizontalAdvance(label) + 16;
        out.append({a, QRectF(right - w, cy - h / 2, w, h)});
        right -= w + 6;
    }
    return out;
}

} // namespace

QList<DiffView::Button> DiffView::headerButtons(const QRectF &r, const FileHeader &h) const {
    QList<Button> out;
    if (!h.actions) return out;
    for (const auto &[a, rect] : layoutButtons(h.actions, false, r.right() - 10, r.center().y(), r.height() - 8))
        out.append({rect, h.path, -1, a});
    return out;
}

QList<DiffView::Button> DiffView::hunkButtons(const QRectF &r, const QString &path, int hunk, int actions) const {
    QList<Button> out;
    if (!actions || hunk < 0) return out;
    const double right = viewport()->width() - 10;
    for (const auto &[a, rect] : layoutButtons(actions, true, right, r.center().y(), r.height() - 2))
        out.append({rect, path, hunk, a});
    return out;
}

QList<DiffView::Button> DiffView::visibleButtons() const {
    QList<Button> out;
    const QPointF offset = contentOffset();
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
        const QRectF r = blockBoundingGeometry(b).translated(offset);
        if (r.top() > viewport()->height()) break;
        const int n = b.blockNumber();
        if (auto fs = m_fileStarts.constFind(n); fs != m_fileStarts.cend())
            out += headerButtons(QRectF(0, r.top(), viewport()->width(), r.height()), fs.value());
        if (auto ha = m_hunkActions.constFind(n); ha != m_hunkActions.cend() && n < m_meta.size())
            out += hunkButtons(r, m_meta[n].path, m_meta[n].hunk, ha.value());
    }
    return out;
}

std::optional<DiffView::Button> DiffView::buttonAt(const QPointF &pos) const {
    if (m_actionSet == NoActions) return std::nullopt;
    FileHeader h;
    const QRectF sticky = stickyHeader(&h);
    if (!sticky.isNull() && sticky.contains(pos)) {   // the sticky header covers what is under it
        for (const Button &b : headerButtons(sticky, h))
            if (b.rect.contains(pos)) return b;
        return std::nullopt;
    }
    for (const Button &b : visibleButtons())
        if (b.rect.contains(pos)) return b;
    return std::nullopt;
}

void DiffView::paintButton(QPainter *p, const Button &b, const QColor &base) const {
    const Settings &st = Settings::instance();
    const bool hover = b.rect == m_hoverButton;
    const bool danger = b.action == DiscardAction || b.action == DeleteAction;
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Settings::mix(base, st.fgColor, hover ? 0.4 : 0.25));
    p->setBrush(Settings::mix(base, hover ? (danger ? removedColor() : st.fgColor) : st.bgColor, hover ? 0.18 : 0.6));
    const QRectF r = b.rect.adjusted(0.5, 0.5, -0.5, -0.5);
    p->drawRoundedRect(r, r.height() * 0.3, r.height() * 0.3);
    p->setFont(buttonFont());
    p->setPen(danger ? removedColor() : st.fgColor);
    p->drawText(b.rect, Qt::AlignCenter, actionLabel(b.action, b.hunk >= 0));
    p->restore();
}

void DiffView::mouseMoveEvent(QMouseEvent *e) {
    if (!m_pressedButton) QPlainTextEdit::mouseMoveEvent(e);
    const auto b = buttonAt(e->position());
    const QRectF hover = b ? b->rect : QRectF();
    if (hover != m_hoverButton) {
        m_hoverButton = hover;
        viewport()->update();
    }
    viewport()->setCursor(b || !anchorAt(e->pos()).isEmpty() ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

void DiffView::leaveEvent(QEvent *e) {
    if (!m_hoverButton.isNull()) {
        m_hoverButton = {};
        viewport()->update();
    }
    QPlainTextEdit::leaveEvent(e);
}

void DiffView::mouseReleaseEvent(QMouseEvent *e) {
    if (m_pressedButton) {
        const Button pressed = *m_pressedButton;
        m_pressedButton.reset();
        e->accept();
        const auto b = buttonAt(e->position());
        if (e->button() == Qt::LeftButton && b && b->path == pressed.path && b->hunk == pressed.hunk &&
            b->action == pressed.action) {
            if (b->hunk >= 0) emit hunkActionRequested(b->path, b->hunk, b->action);
            else emit fileActionRequested(b->path, b->action);
        }
        return;
    }
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
