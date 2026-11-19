#include "ui/Syntax.h"

#include "Settings.h"

#include <QFileInfo>
#include <QHash>

namespace gitq::Syntax {

namespace {

QSet<QString> words(const char *list) {
    const QStringList l = QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return QSet<QString>(l.cbegin(), l.cend());
}

struct Registry {
    QList<Language> languages;
    QHash<QString, int> byExtension;   // lower-case extension without the dot
    QHash<QString, int> byName;        // whole file names (lower case)

    int add(Language l, const char *extensions, const char *names = "") {
        languages.append(std::move(l));
        const int i = int(languages.size()) - 1;
        for (const QString &e : QString::fromLatin1(extensions).split(QLatin1Char(' '), Qt::SkipEmptyParts)) byExtension.insert(e, i);
        for (const QString &n : QString::fromLatin1(names).split(QLatin1Char(' '), Qt::SkipEmptyParts)) byName.insert(n, i);
        return i;
    }

    Registry() {
        const QString cBlock[] = {QStringLiteral("/*"), QStringLiteral("*/")};
        auto cLike = [&](const char *name, const char *kw, const char *types, bool pre = false) {
            Language l;
            l.name = QString::fromLatin1(name);
            l.keywords = words(kw);
            l.types = words(types);
            l.lineComments = {QStringLiteral("//")};
            l.blockStart = cBlock[0];
            l.blockEnd = cBlock[1];
            l.quotes = QStringLiteral("\"'");
            l.preprocessor = pre;
            return l;
        };
        add(cLike("C/C++",
                  "alignas alignof asm auto break case catch class concept const consteval constexpr constinit "
                  "const_cast continue co_await co_return co_yield decltype default delete do dynamic_cast else "
                  "enum explicit export extern final for friend goto if inline mutable namespace new noexcept "
                  "operator override private protected public register reinterpret_cast requires return sizeof "
                  "static static_assert static_cast struct switch template this thread_local throw try typedef "
                  "typeid typename union using virtual volatile while signals slots emit Q_OBJECT",
                  "bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t "
                  "size_t ssize_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t qint64 quint64 "
                  "qint32 quint32 qint16 quint16 qint8 quint8 qreal std true false nullptr NULL",
                  true),
            "c h cpp cc cxx c++ hpp hh hxx h++ ino m mm cu cuh glsl vert frag");
        add(cLike("Java/Kotlin/Scala/C#",
                  "abstract as assert async await base break case catch class companion const continue data def "
                  "default delegate do else enum event extends final finally for foreach fun get if implements "
                  "import in init inline interface internal is lateinit lazy lock match namespace new object open "
                  "operator out override package private protected public readonly record return sealed set static "
                  "super switch synchronized this throw throws trait transient try typeof unsafe using val var "
                  "virtual volatile when where while with yield",
                  "boolean byte char double float int long short void string object String Object Integer Boolean "
                  "Unit Any Nothing true false null"),
            "java kt kts scala sc cs groovy gradle dart");
        add(cLike("JavaScript/TypeScript",
                  "as async await break case catch class const continue debugger declare default delete do else "
                  "enum export extends finally for from function get if implements import in instanceof interface "
                  "keyof let namespace new of private protected public readonly return satisfies set static super "
                  "switch this throw try type typeof var void while with yield",
                  "any bigint boolean never number object string symbol undefined unknown true false null NaN "
                  "Infinity Array Object Promise Map Set"),
            "js jsx mjs cjs ts tsx mts cts qml");
        {
            Language l = cLike("Go",
                               "break case chan const continue default defer else fallthrough for func go goto if "
                               "import interface map package range return select struct switch type var",
                               "bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
                               "string uint uint8 uint16 uint32 uint64 uintptr true false nil iota");
            l.quotes = QStringLiteral("\"'`");
            add(l, "go");
        }
        add(cLike("Rust",
                  "as async await break const continue crate dyn else enum extern fn for if impl in let loop match "
                  "mod move mut pub ref return self Self static struct super trait type unsafe use where while",
                  "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize String Vec Option "
                  "Result Box Some None Ok Err true false"),
            "rs");
        languages.last().quotes = QStringLiteral("\"");   // 'a is a lifetime more often than 'c' a char
        add(cLike("Swift",
                  "associatedtype break case catch class continue default defer deinit do else enum extension "
                  "fallthrough fileprivate for func guard if import in init inout internal is let open operator "
                  "private protocol public repeat rethrows return self Self static struct subscript super switch "
                  "throw throws try typealias var where while",
                  "Bool Character Double Float Int String Void true false nil"),
            "swift");
        add(cLike("PHP",
                  "abstract and as break case catch class clone const continue declare default do echo else elseif "
                  "enum extends final finally fn for foreach function global if implements include interface "
                  "match namespace new or private protected public readonly require return static switch throw "
                  "trait try use var while yield",
                  "array bool float int mixed string void true false null"),
            "php");
        languages.last().dollarVariables = true;
        languages.last().lineComments << QStringLiteral("#");
        add(cLike("CSS", "important media import keyframes supports font-face", ""), "css scss less");
        {
            Language l = cLike("Protobuf/Thrift", "enum extend import message oneof option package repeated required "
                                                  "optional reserved returns rpc service syntax struct",
                               "bool bytes double fixed32 fixed64 float int32 int64 sint32 sint64 string uint32 uint64");
            add(l, "proto thrift");
        }
        {
            Language l;
            l.name = QStringLiteral("Python");
            l.keywords = words("and as assert async await break class continue def del elif else except finally for "
                               "from global if import in is lambda match case nonlocal not or pass raise return try "
                               "while with yield self cls");
            l.types = words("True False None int float str bytes bool list dict set tuple object type print len "
                            "range isinstance super");
            l.lineComments = {QStringLiteral("#")};
            l.quotes = QStringLiteral("\"'");
            l.tripleQuotes = true;
            add(l, "py pyw pyi pyx", "sconstruct sconscript");
        }
        {
            Language l;
            l.name = QStringLiteral("Shell");
            l.keywords = words("if then else elif fi case esac for while until do done in function select time "
                               "return break continue local export readonly declare typeset unset shift source "
                               "alias eval exec exit trap set");
            l.types = words("true false echo printf cd test read");
            l.lineComments = {QStringLiteral("#")};
            l.quotes = QStringLiteral("\"'`");
            l.dollarVariables = true;
            add(l, "sh bash zsh ksh fish bats", ".bashrc .profile .zshrc pkgbuild");
        }
        {
            Language l;
            l.name = QStringLiteral("Ruby");
            l.keywords = words("alias and begin break case class def defined? do else elsif end ensure for if in "
                               "module next not or redo rescue retry return self super then undef unless until when "
                               "while yield require attr_accessor attr_reader private protected public");
            l.types = words("true false nil");
            l.lineComments = {QStringLiteral("#")};
            l.quotes = QStringLiteral("\"'`");
            add(l, "rb rake gemspec", "rakefile gemfile");
        }
        {
            Language l;
            l.name = QStringLiteral("Perl");
            l.keywords = words("my our local sub if elsif else unless while until for foreach do last next redo "
                               "return use no package require and or not eq ne lt gt le ge");
            l.lineComments = {QStringLiteral("#")};
            l.quotes = QStringLiteral("\"'`");
            l.dollarVariables = true;
            add(l, "pl pm t");
        }
        {
            Language l;
            l.name = QStringLiteral("Tcl");
            l.keywords = words("proc set if else elseif for foreach while switch return global upvar variable "
                               "namespace package source expr eval catch error break continue incr list lappend "
                               "lindex llength string regexp regsub bind");
            l.lineComments = {QStringLiteral("#")};
            l.quotes = QStringLiteral("\"");
            l.dollarVariables = true;
            add(l, "tcl tk", "gitk");
        }
        {
            Language l;
            l.name = QStringLiteral("CMake");
            l.keywords = words("if elseif else endif foreach endforeach while endwhile function endfunction macro "
                               "endmacro return set unset option include project add_executable add_library "
                               "add_subdirectory add_test target_link_libraries target_include_directories "
                               "target_compile_options target_compile_definitions find_package install message "
                               "list string file enable_testing cmake_minimum_required");
            l.types = words("ON OFF TRUE FALSE PUBLIC PRIVATE INTERFACE REQUIRED COMPONENTS STATIC SHARED");
            l.lineComments = {QStringLiteral("#")};
            l.quotes = QStringLiteral("\"");
            l.dollarVariables = true;
            l.caseInsensitive = true;
            add(l, "cmake", "cmakelists.txt");
        }
        {
            Language l;
            l.name = QStringLiteral("Make");
            l.keywords = words("ifeq ifneq ifdef ifndef else endif include define endef export override");
            l.lineComments = {QStringLiteral("#")};
            l.quotes = QStringLiteral("\"'");
            l.dollarVariables = true;
            add(l, "mk mak make", "makefile gnumakefile");
        }
        {
            Language l;
            l.name = QStringLiteral("Config");
            l.types = words("true false yes no on off null");
            l.lineComments = {QStringLiteral("#"), QStringLiteral(";")};
            l.quotes = QStringLiteral("\"'");
            add(l, "yml yaml toml ini cfg conf properties desktop env service gitmodules gitconfig editorconfig",
                ".gitignore .gitattributes .gitmodules dockerfile containerfile");
        }
        {
            Language l;
            l.name = QStringLiteral("SQL");
            l.keywords = words("select from where insert into values update set delete create alter drop table "
                               "index view join inner outer left right on group by order having limit offset union "
                               "all distinct as and or not null is in like between exists case when then else end "
                               "primary key foreign references default begin commit rollback transaction");
            l.types = words("int integer bigint smallint varchar char text boolean date timestamp real float "
                            "double numeric decimal blob true false");
            l.lineComments = {QStringLiteral("--")};
            l.blockStart = QStringLiteral("/*");
            l.blockEnd = QStringLiteral("*/");
            l.quotes = QStringLiteral("'\"");
            l.caseInsensitive = true;
            add(l, "sql");
        }
        {
            Language l;
            l.name = QStringLiteral("Lua/Haskell");
            l.keywords = words("and break do else elseif end for function goto if in local not or repeat return "
                               "then until while case class data deriving import instance let module newtype of "
                               "where type");
            l.types = words("true false nil");
            l.lineComments = {QStringLiteral("--")};
            l.quotes = QStringLiteral("\"'");
            add(l, "lua hs");
        }
        {
            Language l;
            l.name = QStringLiteral("JSON");
            l.types = words("true false null");
            l.quotes = QStringLiteral("\"");
            add(l, "json jsonc geojson babelrc eslintrc");
        }
        {
            Language l;
            l.name = QStringLiteral("Markup");
            l.blockStart = QStringLiteral("<!--");
            l.blockEnd = QStringLiteral("-->");
            l.quotes = QStringLiteral("\"'");
            l.markup = true;
            add(l, "xml html htm xhtml svg ui qrc xsd xsl xslt plist vue");
        }
    }
};

const Registry &registry() {
    static const Registry r;
    return r;
}

bool isIdentStart(QChar c) { return c.isLetter() || c == QLatin1Char('_'); }
bool isIdent(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

enum State { Normal = 0, InBlockComment = 1, InTripleDouble = 2, InTripleSingle = 3 };

} // namespace

const Language *languageFor(const QString &path) {
    const Registry &r = registry();
    const QFileInfo fi(path);
    const QString name = fi.fileName().toLower();
    if (auto it = r.byName.constFind(name); it != r.byName.cend()) return &r.languages[*it];
    if (name.startsWith(QLatin1String("dockerfile")) || name.startsWith(QLatin1String("makefile")))
        return &r.languages[r.byName.value(name.startsWith(QLatin1Char('d')) ? QStringLiteral("dockerfile")
                                                                              : QStringLiteral("makefile"))];
    const QString ext = fi.suffix().toLower();
    if (auto it = r.byExtension.constFind(ext); !ext.isEmpty() && it != r.byExtension.cend()) return &r.languages[*it];
    return nullptr;
}

int highlightLine(const Language &lang, const QString &text, int from, int state, QList<Span> *out) {
    const int n = int(text.size());
    int i = from;
    auto span = [&](int start, int end, Token t) {
        if (end > start) out->append({start, end - start, t});
    };
    auto closeMultiline = [&](const QString &close, Token t, int resume) -> bool {
        const int end = int(text.indexOf(close, i));
        if (end < 0) {
            span(i, n, t);
            i = n;
            return false;
        }
        span(i, end + int(close.size()), t);
        i = end + int(close.size());
        state = resume;
        return true;
    };
    if (state == InBlockComment && !closeMultiline(lang.blockEnd, Comment, Normal)) return InBlockComment;
    if (state == InTripleDouble && !closeMultiline(QStringLiteral("\"\"\""), String, Normal)) return InTripleDouble;
    if (state == InTripleSingle && !closeMultiline(QStringLiteral("'''"), String, Normal)) return InTripleSingle;
    state = Normal;

    // Preprocessor lines (C): the whole line, apart from strings and comments handled below.
    int firstNonSpace = from;
    while (firstNonSpace < n && text[firstNonSpace].isSpace()) ++firstNonSpace;
    if (lang.preprocessor && i == from && firstNonSpace < n && text[firstNonSpace] == QLatin1Char('#')) {
        int end = firstNonSpace + 1;
        while (end < n && text[end].isSpace()) ++end;
        while (end < n && isIdent(text[end])) ++end;
        span(firstNonSpace, end, Preprocessor);
        i = end;
    }

    bool inTag = false;   // markup: inside <...>
    while (i < n) {
        const QChar c = text[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        // Comments.
        bool comment = false;
        for (const QString &lc : lang.lineComments)
            if (text.mid(i, lc.size()) == lc) {
                // "#" starts a comment in shell-like languages only at a word boundary ("a#b" isn't one).
                if (lc == QLatin1String("#") && i > from && !text[i - 1].isSpace() && lang.dollarVariables) continue;
                span(i, n, Comment);
                i = n;
                comment = true;
                break;
            }
        if (comment) break;
        if (!lang.blockStart.isEmpty() && text.mid(i, lang.blockStart.size()) == lang.blockStart) {
            const int start = i;
            i += int(lang.blockStart.size());
            const int end = int(text.indexOf(lang.blockEnd, i));
            if (end < 0) {
                span(start, n, Comment);
                return InBlockComment;
            }
            span(start, end + int(lang.blockEnd.size()), Comment);
            i = end + int(lang.blockEnd.size());
            continue;
        }
        // Strings.
        if (lang.quotes.contains(c) && (!lang.markup || inTag)) {
            if (lang.tripleQuotes && text.mid(i, 3) == QString(3, c)) {
                const QString close(3, c);
                const int start = i;
                const int end = int(text.indexOf(close, i + 3));
                if (end < 0) {
                    span(start, n, String);
                    return c == QLatin1Char('"') ? InTripleDouble : InTripleSingle;
                }
                span(start, end + 3, String);
                i = end + 3;
                continue;
            }
            // In C-like languages 'x' is a character literal; in Rust 'a can be a lifetime.
            const int start = i++;
            while (i < n && text[i] != c) i += text[i] == QLatin1Char('\\') ? 2 : 1;
            i = qMin(n, i + 1);
            span(start, i, String);
            continue;
        }
        // Markup tags.
        if (lang.markup) {
            if (c == QLatin1Char('<') && i + 1 < n &&
                (text[i + 1].isLetter() || text[i + 1] == QLatin1Char('/') || text[i + 1] == QLatin1Char('!') ||
                 text[i + 1] == QLatin1Char('?'))) {
                const int start = i++;
                while (i < n && (text[i] == QLatin1Char('/') || text[i] == QLatin1Char('!') || text[i] == QLatin1Char('?'))) ++i;
                while (i < n && (isIdent(text[i]) || text[i] == QLatin1Char(':') || text[i] == QLatin1Char('-') ||
                                 text[i] == QLatin1Char('.')))
                    ++i;
                span(start, i, Tag);
                inTag = true;
                continue;
            }
            if (inTag && (c == QLatin1Char('>') || text.mid(i, 2) == QLatin1String("/>"))) {
                const int len = c == QLatin1Char('>') ? 1 : 2;
                span(i, i + len, Tag);
                i += len;
                inTag = false;
                continue;
            }
            if (inTag && isIdentStart(c)) {
                const int start = i;
                while (i < n && (isIdent(text[i]) || text[i] == QLatin1Char(':') || text[i] == QLatin1Char('-'))) ++i;
                span(start, i, Type);   // attribute names
                continue;
            }
            if (c == QLatin1Char('&')) {   // entities
                const int end = int(text.indexOf(QLatin1Char(';'), i));
                if (end > i && end - i < 10) {
                    span(i, end + 1, Number);
                    i = end + 1;
                    continue;
                }
            }
            ++i;
            continue;
        }
        // Variables: $name, ${name}, $(name).
        if (lang.dollarVariables && c == QLatin1Char('$') && i + 1 < n) {
            const int start = i++;
            if (text[i] == QLatin1Char('{') || text[i] == QLatin1Char('(')) {
                const QChar close = text[i] == QLatin1Char('{') ? QLatin1Char('}') : QLatin1Char(')');
                const int end = int(text.indexOf(close, i));
                i = end < 0 ? n : end + 1;
            } else {
                while (i < n && isIdent(text[i])) ++i;
                if (i == start + 1 && i < n) ++i;   // $1, $@, $?
            }
            span(start, i, Type);
            continue;
        }
        // Numbers.
        if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && text[i + 1].isDigit())) {
            if (i > from && isIdent(text[i - 1])) {   // part of an identifier
                ++i;
                continue;
            }
            const int start = i++;
            while (i < n && (text[i].isLetterOrNumber() || text[i] == QLatin1Char('.') || text[i] == QLatin1Char('_') ||
                             text[i] == QLatin1Char('\'')))
                ++i;
            span(start, i, Number);
            continue;
        }
        // Words: keywords, types, function calls.
        if (isIdentStart(c) || c == QLatin1Char('@')) {
            const int start = i++;
            while (i < n && (isIdent(text[i]) || (lang.name == QLatin1String("Ruby") && text[i] == QLatin1Char('?')))) ++i;
            QString word = text.mid(start, i - start);
            if (lang.caseInsensitive) word = word.toLower();
            if (lang.keywords.contains(word)) {
                span(start, i, Keyword);
            } else if (lang.types.contains(word) || (lang.caseInsensitive && lang.types.contains(word.toUpper()))) {
                span(start, i, Type);
            } else if (c == QLatin1Char('@')) {
                span(start, i, Preprocessor);   // decorators, annotations
            } else {
                int j = i;
                while (j < n && text[j] == QLatin1Char(' ')) ++j;
                if (j < n && text[j] == QLatin1Char('(')) span(start, i, Function);
            }
            continue;
        }
        ++i;
    }
    return Normal;
}

QColor color(Token t) {
    const bool dark = Settings::darkTheme();
    switch (t) {
    case Keyword: return dark ? QColor(0xff, 0x7b, 0x72) : QColor(0xcf, 0x22, 0x2e);
    case Type: return dark ? QColor(0xff, 0xa6, 0x57) : QColor(0x95, 0x38, 0x00);
    case String: return dark ? QColor(0xa5, 0xd6, 0xff) : QColor(0x0a, 0x6e, 0x8a);
    case Comment: return dark ? QColor(0x8b, 0x94, 0x9e) : QColor(0x6e, 0x77, 0x81);
    case Number: return dark ? QColor(0x79, 0xc0, 0xff) : QColor(0x05, 0x50, 0xae);
    case Preprocessor: return dark ? QColor(0xff, 0x7b, 0x72) : QColor(0xcf, 0x22, 0x2e);
    case Function: return dark ? QColor(0xd2, 0xa8, 0xff) : QColor(0x82, 0x50, 0xdf);
    case Tag: return dark ? QColor(0x7e, 0xe7, 0x87) : QColor(0x11, 0x63, 0x29);
    case TokenCount: break;
    }
    return Settings::instance().fgColor;
}

} // namespace gitq::Syntax
