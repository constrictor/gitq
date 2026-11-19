// A small, dependency-free syntax highlighter for the diff pane: keywords, types, strings,
// comments, numbers, preprocessor lines, function names and markup tags for common languages,
// chosen by file name. It works line by line with a small state for constructs spanning lines
// (block comments, triple-quoted strings), so that the old and new sides of a diff can each be
// followed separately.
#pragma once

#include <QColor>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace gitq {

namespace Syntax {

enum Token : quint8 { Keyword, Type, String, Comment, Number, Preprocessor, Function, Tag, TokenCount };

struct Span {
    int start;
    int len;
    Token token;
};

struct Language {
    QString name;
    QSet<QString> keywords;
    QSet<QString> types;            // built-in types and constants
    QStringList lineComments;       // "//", "#", "--", ...
    QString blockStart, blockEnd;   // "/*" "*/", "<!--" "-->"
    QString quotes;                 // string delimiters
    bool tripleQuotes = false;      // Python-style """ and '''
    bool preprocessor = false;      // lines starting with '#'
    bool markup = false;            // <tags attr="...">
    bool caseInsensitive = false;   // keywords (SQL)
    bool dollarVariables = false;   // $name, ${name} (shell, Perl, PHP, Makefiles)
};

// The language of a file, by its name; null if unknown.
const Language *languageFor(const QString &path);

// Highlights `text` from position `from` on, starting in `state` (0 at the start of a file);
// appends the spans found and returns the state at the end of the line.
int highlightLine(const Language &lang, const QString &text, int from, int state, QList<Span> *out);

// Colours of the tokens for the current theme.
QColor color(Token t);

} // namespace Syntax

} // namespace gitq
