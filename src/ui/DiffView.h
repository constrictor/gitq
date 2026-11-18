// The commit details + diff text pane (gitk's lower-left pane).
#pragma once

#include "git/CommitInfo.h"
#include "git/DiffEngine.h"
#include "git/NearbyRefs.h"
#include "git/Search.h"

#include <QHash>
#include <QMap>
#include <QPlainTextEdit>

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
    void showHeader(const CommitInfo &c, const QList<RefInfo> &refs, const QList<Link> &parents,
                    const QList<Link> &children, const QString &dateFormat);
    void showRangeHeader(const QString &text);
    void showNearby(const NearbyRefs &nearby, int maxRefs);
    void appendDiff(const DiffResult &diff);
    void showFileContents(const QString &path, const QString &text);

    void setMode(Mode m) { m_mode = m; }
    void setWordMode(WordMode m) { m_wordMode = m; }

    void scrollToFile(const QString &path);
    void scrollToTop();
    QString fileAtTop() const;
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
    void fileAtTopChanged(const QString &path);
    void diffContextMenu(const gitq::DiffLineInfo &info, const QPoint &globalPos);

protected:
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    friend class DiffHighlighter;
    enum Style : quint8 { HeaderStyle, PlainStyle, OldStyle, NewStyle, HunkStyle, FileSepStyle, ErrorStyle };
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
    void renderFile(const FileDiff &f);
    void renderWordRun(const QStringList &dels, const QStringList &adds, const QString &path, int &oldNo, int &newNo);
    void updateExtraSelections();
    void onScroll();

    Mode m_mode = ShowDiff;
    WordMode m_wordMode = LineDiff;
    QList<DiffLineInfo> m_meta;             // per block
    QList<quint8> m_style;                  // per block
    QHash<int, QList<WordRange>> m_wordRanges;
    QStringList m_pending;                  // body lines not yet inserted
    QMap<int, QString> m_fileStarts;        // block number -> path
    int m_headerEnd = 0;                    // block number where the comment ends
    int m_nearbyBlock = -1;                 // block number where nearby refs are inserted
    QString m_searchText;
    FindSpec m_findSpec;
    QString m_lastTopFile;
    QTextCursor m_currentSearchHit;
};

} // namespace gitq
