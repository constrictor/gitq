#include "ui/DiffView.h"

#include "Settings.h"

#include <QContextMenuEvent>
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

void DiffView::showHeader(const CommitInfo &c, const QList<RefInfo> &refs, const QList<Link> &parents,
                          const QList<Link> &children, const QString &dateFormat) {
    clearAll();
    const QTextCharFormat plain = plainFormat();
    auto date = [&](const Person &p) { return QDateTime::fromSecsSinceEpoch(p.time).toString(dateFormat); };

    if (c.id.isPseudo()) {
        appendLine(c.subject, plain);
    } else {
        appendLine(QStringLiteral("Author: %1  %2").arg(c.author.nameEmail(), date(c.author)), plain);
        appendLine(QStringLiteral("Committer: %1  %2").arg(c.committer.nameEmail(), date(c.committer)), plain);
    }
    QStringList tags;
    for (const RefInfo &r : refs)
        if (r.kind == RefInfo::Tag) tags << r.name;
    if (!tags.isEmpty()) appendLine(QStringLiteral("Tags: ") + tags.join(QStringLiteral(", ")), plain);

    auto linkLines = [&](const QString &label, const QList<Link> &links) {
        for (const Link &l : links) {
            appendLine(label, plain);
            QTextCursor cur(document());
            cur.movePosition(QTextCursor::End);
            appendLink(cur, l.first.isPseudo() ? (l.first.isWorkdir() ? tr("Local uncommitted changes")
                                                                      : tr("Local changes in index"))
                                               : l.first.hex(),
                       l.first);
            if (!l.second.isEmpty()) cur.insertText(QStringLiteral(" (%1)").arg(l.second), plain);
        }
    };
    linkLines(tr("Parent: "), parents);
    linkLines(tr("Child:  "), children);
    m_nearbyBlock = int(m_meta.size());

    if (!c.id.isPseudo()) {
        appendLine(QString(), plain);
        QString msg = c.message();
        while (msg.endsWith(QLatin1Char('\n'))) msg.chop(1);
        for (const QString &line : msg.split(QLatin1Char('\n'))) appendLine(QStringLiteral("    ") + line, plain);
    }
    appendLine(QString(), plain);
    m_headerEnd = int(m_meta.size());
    updateExtraSelections();
    scrollToTop();
}

void DiffView::showRangeHeader(const QString &text) {
    clearAll();
    appendLine(text, plainFormat());
    appendLine(QString(), plainFormat());
    m_headerEnd = int(m_meta.size());
}

void DiffView::showNearby(const NearbyRefs &n, int maxRefs) {
    if (m_nearbyBlock < 0) return;
    QStringList lines;
    auto fmt = [&](const QString &label, const QStringList &names) {
        if (names.isEmpty()) return;
        QStringList shown = names.mid(0, maxRefs);
        QString s = label + shown.join(QStringLiteral(", "));
        if (names.size() > maxRefs) s += QStringLiteral(", ...");
        lines << s;
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
    for (const QString &l : lines) {
        cur.insertBlock(QTextBlockFormat());
        cur.insertText(l, plainFormat());
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
    m_nearbyBlock = -1;
    verticalScrollBar()->setValue(keep);
    updateExtraSelections();
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
