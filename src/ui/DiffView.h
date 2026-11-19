// The commit details + diff text pane (gitk's lower-left pane).
#pragma once

#include "git/CommitInfo.h"
#include "git/DiffEngine.h"
#include "git/NearbyRefs.h"
#include "git/Search.h"
#include "ui/RefPills.h"

#include <QHash>
#include <QMap>
#include <QPlainTextEdit>

class QPainter;
class QTimer;

namespace gitq {

struct DiffLineInfo {
    QString path;
    QChar kind;        // '+', '-', ' ' or null for non-content lines
    int oldLine = 0;   // 1-based, 0 if n/a
    int newLine = 0;
};

class DiffView : public QPlainTextEdit {
    Q_OBJECT
public:
    enum Mode { ShowDiff, ShowOld, ShowNew };
    enum WordMode { LineDiff, MarkupWords, ColorWords };

    explicit DiffView(QWidget *parent = nullptr);

    void clearAll();
    // Commit header (author, parents, children, message). `refs` are the refs at this commit.
    using Link = QPair<Oid, QString>;   // id, headline
    // The header is a "card": the author's initials in a circle, the subject as a heading, author
    // and dates, the commit id (click to copy), ref labels, parents and children; then the message.
    void showHeader(const CommitInfo &c, const QList<RefInfo> &refs, const QString &headBranch,
                    const QList<Link> &parents, const QList<Link> &children, const QString &dateFormat);
    void showRangeHeader(const QString &text);
    void showNearby(const NearbyRefs &nearby, int maxRefs);
    void appendDiff(const DiffResult &diff);
    void showFileContents(const QString &path, const QString &text);

    void setMode(Mode m) { m_mode = m; }
    void setWordMode(WordMode m) { m_wordMode = m; }
    // Side by side: old and new versions in two columns (two-way diffs in Diff mode; the line /
    // word diff choice doesn't apply). Takes effect with the next appendDiff().
    void setSideBySide(bool on);
    bool sideBySide() const { return m_sideBySide; }
    // Whether submodule entries offer to show / hide their files (DiffOptions::SubmoduleLog).
    void setSubmoduleToggles(bool on) { m_submoduleToggles = on; }

    void scrollToFile(const QString &path);
    void scrollToTop();
    QString fileAtTop() const;
    // Paths of the files shown, in order.
    QStringList files() const;
    DiffLineInfo lineInfoAt(const QPoint &viewportPos) const;
    // Scroll to the given line of a file's diff (new side when `newSide`), returns success.
    bool scrollToLine(const QString &path, int line, bool newSide);

    // Diff-pane search (gitk's "Search" field).
    void setSearchText(const QString &text);
    bool searchNext(bool backward);
    // Highlight find-bar matches in the comment part.
    void setFindSpec(const FindSpec &spec);

    void applyFonts();

    struct WordRange {
        int start;
        int len;
        quint8 kind;   // OldStyle / NewStyle
    };
    // Ranges of the words that differ between two versions of a line, positions shifted by
    // `offset` (the +/- mark); returns how similar the lines are (0: nothing in common, 1: same).
    static double changedWords(const QString &oldText, const QString &newText, int offset, QList<WordRange> *oldRanges,
                               QList<WordRange> *newRanges);
    // Pairs (old index, new index) of similar lines in a change, in order.
    static QList<QPair<int, int>> pairSimilarLines(const QStringList &olds, const QStringList &news);

signals:
    void commitLinkActivated(const gitq::Oid &id);
    // Show / hide the file changes of the submodule at `path`.
    void submoduleToggled(const QString &path);
    // Open the submodule at `path` in a tab, at `commit` (null: the commit it was changed to).
    void submoduleOpenRequested(const QString &path, const gitq::Oid &commit);
    void fileAtTopChanged(const QString &path);
    void diffContextMenu(const gitq::DiffLineInfo &info, const QPoint &globalPos);
    void statusMessage(const QString &text, int timeout);

protected:
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool viewportEvent(QEvent *e) override;

private:
    friend class DiffHighlighter;
    friend class HeaderObjects;
    friend class LineGutter;
    enum Style : quint8 {
        HeaderStyle, PlainStyle, OldStyle, NewStyle, HunkStyle, FileSepStyle, ErrorStyle, MetaStyle,
        SplitStyle,   // a side-by-side row; see m_splitRows
    };
    // A side-by-side row: the old side in the first m_splitWidth characters, the new side after
    // kSplitMiddle more. Kinds: '-' removed, '+' added, ' ' context, null for no line.
    struct SplitRow {
        QChar left, right;
    };
    static constexpr int kSplitMiddle = 8;   // characters between the sides (new line numbers)
    // A file's header bar: status badge, path, line counts.
    struct FileHeader {
        QString path;
        QString title;      // path, or "old → new" for renames and copies
        QChar status;       // A, M, D, R, C, T; null in Tree mode
        int added = 0, removed = 0;
        bool binary = false;
        bool submodule = false;
    };
    // Header lines are inserted directly with formats (they contain links).
    void appendLine(const QString &text, const QTextCharFormat &fmt, const DiffLineInfo &meta = {},
                    const QTextBlockFormat &bfmt = QTextBlockFormat());
    // Body lines are buffered and inserted in one go by flushBody(); colours come from the highlighter.
    void addBodyLine(const QString &text, quint8 style, const DiffLineInfo &meta, const QList<WordRange> &words = {},
                     int markLen = 0);
    void flushBody();
    void appendLink(QTextCursor &cur, const QString &label, const Oid &id);
    // Header card lines: a new block indented past the avatar, in the card's font.
    QTextCursor appendCardLine(const QTextCharFormat &fmt, bool indent = true);
    QTextCharFormat cardFormat(double scale = 1.0, bool bold = false, bool dim = false) const;
    void insertObject(QTextCursor &cur, int kind, int index, const QString &href = QString());
    void renderFile(const FileDiff &f);
    void renderSplitFile(const FileDiff &f, const DiffLineInfo &fileMeta);
    // Side-by-side rows of one old and one new line (either may be absent), wrapped to the
    // column width.
    struct SideLine {
        QChar kind;   // '-', '+', ' ' or null
        QString text;
        int line = 0;
    };
    void addSplitRows(const QString &path, const SideLine &left, const SideLine &right);
    int splitColumns() const;   // characters per side for the current width
    // Re-renders the diff below the header (the column width changed).
    void rerenderBody();
    // The x range of the new side, in viewport coordinates.
    double splitRightX() const;
    // Background of changed words (and of the gutter beside changed lines).
    static QColor wordBg(bool old);
    void renderSubmodule(const SubmoduleChange &s);
    void appendLinkTo(QTextCursor &cur, const QString &label, const QString &href);
    void renderWordRun(const QStringList &dels, const QStringList &adds, const QString &path, int &oldNo, int &newNo);
    void updateExtraSelections();
    // Line number gutter left of the text.
    int gutterWidth() const;
    void updateGutter();
    void paintGutter(QPaintEvent *e);
    // Paints a file header bar into `r`.
    void paintFileHeader(QPainter *p, const QRectF &r, const FileHeader &h) const;
    // The sticky header's rectangle and file at the top of the viewport (null rect if none).
    QRectF stickyHeader(FileHeader *out = nullptr) const;
    void onScroll();

    Mode m_mode = ShowDiff;
    bool m_sideBySide = false;
    int m_splitWidth = 0;                   // characters per side of the rendered split rows (0: none)
    QHash<int, SplitRow> m_splitRows;       // block number -> side-by-side row
    DiffResult m_lastResult;                // for re-rendering when the width changes
    bool m_hasResult = false;
    QTimer *m_resplitTimer = nullptr;
    WordMode m_wordMode = LineDiff;
    bool m_submoduleToggles = false;
    QList<DiffLineInfo> m_meta;             // per block
    QList<quint8> m_style;                  // per block
    QList<quint8> m_markLen;                // per block: length of the +/- marks at its start
    QHash<int, QList<WordRange>> m_wordRanges;
    QStringList m_pending;                  // body lines not yet inserted
    QMap<int, FileHeader> m_fileStarts;     // block number of each file's header
    int m_headerEnd = 0;                    // block number where the comment ends
    int m_cardEnd = 0;                      // block number where the header card ends
    QString m_avatarText;                   // initials in the card's avatar (empty: none)
    QColor m_avatarColor;
    QList<RefPill> m_cardPills;             // ref labels in the card
    QString m_cardId;                       // full id shown in the card's id chip
    QWidget *m_gutter = nullptr;
    int m_gutterDigits = 0;                 // 0: no line numbers to show
    bool m_singleNumberColumn = false;      // file contents (Tree mode): one number per line
    int m_nearbyBlock = -1;                 // block number where nearby refs are inserted
    QString m_searchText;
    FindSpec m_findSpec;
    QString m_lastTopFile;
    QTextCursor m_currentSearchHit;
};

} // namespace gitq
