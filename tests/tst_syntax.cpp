// The diff pane's syntax highlighter.
#include "ui/Syntax.h"

#include <QTest>

using namespace gitq;
using namespace gitq::Syntax;

class TestSyntax : public QObject {
    Q_OBJECT

    // "token:text" for each span, for compact comparisons.
    static QStringList tokens(const QString &file, const QString &text, int from = 0, int state = 0, int *outState = nullptr) {
        const Language *lang = languageFor(file);
        if (!lang) qFatal("no language for %s", qPrintable(file));
        QList<Span> spans;
        const int s = highlightLine(*lang, text, from, state, &spans);
        if (outState) *outState = s;
        static const char *const names[] = {"kw", "type", "str", "com", "num", "pre", "fn", "tag"};
        QStringList out;
        for (const Span &sp : spans) out << QStringLiteral("%1:%2").arg(names[sp.token], text.mid(sp.start, sp.len));
        return out;
    }

private slots:
    void languages() {
        QCOMPARE(languageFor("src/main.cpp")->name, QString("C/C++"));
        QCOMPARE(languageFor("a/b/util.py")->name, QString("Python"));
        QCOMPARE(languageFor("CMakeLists.txt")->name, QString("CMake"));
        QCOMPARE(languageFor("Makefile")->name, QString("Make"));
        QCOMPARE(languageFor("x.ts")->name, QString("JavaScript/TypeScript"));
        QVERIFY(!languageFor("README"));
        QVERIFY(!languageFor("notes.txt"));
    }

    void cpp() {
        QCOMPARE(tokens("a.cpp", "    return foo(\"a\\\"b\", 42); // done"),
                 (QStringList{"kw:return", "fn:foo", "str:\"a\\\"b\"", "num:42", "com:// done"}));
        QCOMPARE(tokens("a.cpp", "#include <QString>"), (QStringList{"pre:#include"}));
        QCOMPARE(tokens("a.cpp", "int x2 = 0x1F;"), (QStringList{"type:int", "num:0x1F"}));
        // From an offset (after the diff's +/- mark).
        QCOMPARE(tokens("a.cpp", "+if (a)", 1), (QStringList{"kw:if"}));
    }

    void multiline() {
        int state = 0;
        QCOMPARE(tokens("a.c", "x = 1; /* start", 0, 0, &state), (QStringList{"num:1", "com:/* start"}));
        QVERIFY(state != 0);
        QCOMPARE(tokens("a.c", "still comment", 0, state, &state), (QStringList{"com:still comment"}));
        QCOMPARE(tokens("a.c", "end */ return;", 0, state, &state), (QStringList{"com:end */", "kw:return"}));
        QCOMPARE(state, 0);

        QCOMPARE(tokens("a.py", "    \"\"\"Doc", 0, 0, &state), (QStringList{"str:\"\"\"Doc"}));
        QVERIFY(state != 0);
        QCOMPARE(tokens("a.py", "more.\"\"\" if x:", 0, state, &state), (QStringList{"str:more.\"\"\"", "kw:if"}));
        QCOMPARE(state, 0);
    }

    void scripts() {
        QCOMPARE(tokens("run.sh", "echo \"$HOME\" ${X} # note"), (QStringList{"type:echo", "str:\"$HOME\"", "type:${X}", "com:# note"}));
        QCOMPARE(tokens("q.sql", "SELECT id FROM t -- all"), (QStringList{"kw:SELECT", "kw:FROM", "com:-- all"}));
        QCOMPARE(tokens("a.xml", "<a href=\"x\">t</a>"),
                 (QStringList{"tag:<a", "type:href", "str:\"x\"", "tag:>", "tag:</a", "tag:>"}));
    }
};

QTEST_MAIN(TestSyntax)
#include "tst_syntax.moc"
