#include "TestRepo.h"
#include "git/Search.h"

using namespace gitq;

class TestSearch : public QObject {
    Q_OBJECT
    LibGit2Init init;
private slots:
    void fields() {
        CommitInfo c;
        c.subject = "Fix the parser";
        c.rawMessage = "Fix the parser\n\nDetails about Widgets\n";
        c.author = {"Alice", "alice@example.com", 0, 0};
        c.committer = {"Bob", "bob@example.com", 0, 0};
        FindSpec s;
        s.text = "parser";
        QVERIFY(Search::matchesFields(c, s));
        s.text = "widgets";
        QVERIFY(!Search::matchesFields(c, s));
        s.mode = FindSpec::IgnoreCase;
        QVERIFY(Search::matchesFields(c, s));
        s.field = FindSpec::Headline;
        QVERIFY(!Search::matchesFields(c, s));
        s.field = FindSpec::Author;
        s.text = "alice@";
        QVERIFY(Search::matchesFields(c, s));
        s.field = FindSpec::Committer;
        QVERIFY(!Search::matchesFields(c, s));
        s.mode = FindSpec::Regexp;
        s.field = FindSpec::Headline;
        s.text = "^Fix .* parser$";
        QVERIFY(Search::matchesFields(c, s));
        QCOMPARE(Search::matchRanges("a parser and parser", {FindSpec::Containing, FindSpec::Exact, FindSpec::AllFields, "parser"}).size(), 2);
    }

    void content() {
        test::TestRepo r;
        QString a = r.commit("a", {{"src/x.c", "int foo;\n"}, {"doc/y", "y\n"}});
        QString b = r.commit("b", {{"src/x.c", "int foo;\nint bar;\n"}});
        QString c = r.commit("c", {{"doc/y", "y2 bar\n"}});
        QString d = r.commit("d", {{"src/x.c", "int foo;\nint bar; // tweak\n"}});
        Repository repo = r.open();
        auto ids = [&](FindSpec s) {
            QStringList out;
            for (const QString &id : {a, b, c, d})
                if (Search::matchesContent(repo, Oid::fromHex(id), s)) out << id;
            return out;
        };
        auto gitlog = [&](const QStringList &args) {
            QStringList out = r.git(QStringList{"log", "--format=%H"} << args).split('\n', Qt::SkipEmptyParts);
            std::reverse(out.begin(), out.end());
            return out;
        };
        FindSpec s;
        s.type = FindSpec::TouchingPaths;
        s.text = "src";
        QCOMPARE(ids(s), gitlog({"--", "src"}));
        s.type = FindSpec::AddingRemoving;
        s.text = "bar";
        QCOMPARE(ids(s), gitlog({"-Sbar"}));
        s.type = FindSpec::ChangingLines;
        s.mode = FindSpec::Regexp;
        s.text = "ba[r]";
        QCOMPARE(ids(s), gitlog({"-Gba[r]"}));
    }
};

QTEST_GUILESS_MAIN(TestSearch)
#include "tst_search.moc"
