#include "ui/DiffView.h"

#include "Settings.h"
#include "model/CommitModel.h"
#include "ui/Glyphs.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHelpEvent>
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
    m_nearbyBlock = -1;
    m_lastTopFile.clear();
    m_currentSearchHit = QTextCursor();
    setExtraSelections({});
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
    QMap<int, QString> shifted;
    for (auto it = m_fileStarts.cbegin(); it != m_fileStarts.cend(); ++it) shifted.insert(it.key() + k, it.value());
    m_fileStarts = shifted;
    m_headerEnd += k;
    m_cardEnd += k;
    m_nearbyBlock = -1;
    verticalScrollBar()->setValue(keep);
    updateExtraSelections();
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
    QString title = f.status == QLatin1Char('R') || f.status == QLatin1Char('C')
                        ? QStringLiteral("%1 -> %2").arg(f.oldPath, f.newPath)
                        : f.path();
    m_fileStarts.insert(int(m_meta.size()), f.path());
    DiffLineInfo fileMeta;
    fileMeta.path = f.path();
    addBodyLine(QStringLiteral("---------- %1 ----------").arg(title), FileSepStyle, fileMeta);

    const int n = qMax(1, f.numParents);
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
    m_fileStarts.insert(int(m_meta.size()), s.path);
    DiffLineInfo meta;
    meta.path = s.path;
    addBodyLine(QStringLiteral("---------- %1 (submodule) ----------").arg(s.path), FileSepStyle, meta);
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
    m_fileStarts.insert(0, path);
    addBodyLine(QStringLiteral("---------- %1 ----------").arg(path), FileSepStyle, meta);
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
    flushBody();
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
        if (it.value() != path) continue;
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

QString DiffView::fileAtTop() const {
    int top = firstVisibleBlock().blockNumber();
    auto it = m_fileStarts.upperBound(top);
    if (it == m_fileStarts.cbegin()) return {};
    --it;
    return it.value();
}

void DiffView::onScroll() {
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
    for (auto it = m_fileStarts.cbegin(); it != m_fileStarts.cend(); ++it) {
        QTextEdit::ExtraSelection s;
        s.cursor = QTextCursor(document()->findBlockByNumber(it.key()));
        s.format.setBackground(st.fileSepBgColor);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        sels << s;
    }
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
