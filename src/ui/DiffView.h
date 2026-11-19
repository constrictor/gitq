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
    bool viewportEvent(QEvent *e) override;

private:
    friend class DiffHighlighter;
    friend class HeaderObjects;
    enum Style : quint8 { HeaderStyle, PlainStyle, OldStyle, NewStyle, HunkStyle, FileSepStyle, ErrorStyle };
    // A file's header bar: status badge, path, line counts.
    struct FileHeader {
        QString path;
        QString title;      // path, or "old → new" for renames and copies
        QChar status;       // A, M, D, R, C, T; null in Tree mode
        int added = 0, removed = 0;
        bool binary = false;
        bool submodule = false;
    };
    struct WordRange {
        int start;
        int len;
        quint8 kind;   // OldStyle / NewStyle
    };

    // Header lines are inserted directly with formats (they contain links).
    void appendLine(const QString &text, const QTextCharFormat &fmt, const DiffLineInfo &meta = {},
                    const QTextBlockFormat &bfmt = QTextBlockFormat());
    // Body lines are buffered and inserted in one go by flushBody(); colours come from the highlighter.
    void addBodyLine(const QString &text, quint8 style, const DiffLineInfo &meta, const QList<WordRange> &words = {});
    void flushBody();
    void appendLink(QTextCursor &cur, const QString &label, const Oid &id);
    // Header card lines: a new block indented past the avatar, in the card's font.
    QTextCursor appendCardLine(const QTextCharFormat &fmt, bool indent = true);
    QTextCharFormat cardFormat(double scale = 1.0, bool bold = false, bool dim = false) const;
    void insertObject(QTextCursor &cur, int kind, int index, const QString &href = QString());
    void renderFile(const FileDiff &f);
    void renderSubmodule(const SubmoduleChange &s);
    void appendLinkTo(QTextCursor &cur, const QString &label, const QString &href);
    void renderWordRun(const QStringList &dels, const QStringList &adds, const QString &path, int &oldNo, int &newNo);
    void updateExtraSelections();
    // Paints a file header bar into `r`.
    void paintFileHeader(QPainter *p, const QRectF &r, const FileHeader &h) const;
    // The sticky header's rectangle and file at the top of the viewport (null rect if none).
    QRectF stickyHeader(FileHeader *out = nullptr) const;
    void onScroll();

    Mode m_mode = ShowDiff;
    WordMode m_wordMode = LineDiff;
    bool m_submoduleToggles = false;
    QList<DiffLineInfo> m_meta;             // per block
    QList<quint8> m_style;                  // per block
    QHash<int, QList<WordRange>> m_wordRanges;
    QStringList m_pending;                  // body lines not yet inserted
    QMap<int, FileHeader> m_fileStarts;     // block number of each file's header
    int m_headerEnd = 0;                    // block number where the comment ends
    int m_cardEnd = 0;                      // block number where the header card ends
    QString m_avatarText;                   // initials in the card's avatar (empty: none)
    QColor m_avatarColor;
    QList<RefPill> m_cardPills;             // ref labels in the card
    QString m_cardId;                       // full id shown in the card's id chip
    int m_nearbyBlock = -1;                 // block number where nearby refs are inserted
    QString m_searchText;
    FindSpec m_findSpec;
    QString m_lastTopFile;
    QTextCursor m_currentSearchHit;
};

} // namespace gitq
