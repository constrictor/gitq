// Commit list model: date display.
#include "model/CommitModel.h"

#include <QTest>

using namespace gitq;

class TestCommitModel : public QObject {
    Q_OBJECT
private slots:
    void relativeTime_data() {
        QTest::addColumn<qint64>("ago");
        QTest::addColumn<QString>("text");
        const qint64 min = 60, hour = 3600, day = 86400;
        QTest::newRow("now") << qint64(10) << "just now";
        QTest::newRow("minute") << min << "1 minute ago";
        QTest::newRow("minutes") << 25 * min << "25 minutes ago";
        QTest::newRow("hours") << 5 * hour << "5 hours ago";
        QTest::newRow("yesterday") << day + 2 * hour << "yesterday";
        QTest::newRow("days") << 4 * day << "4 days ago";
        QTest::newRow("weeks") << 20 * day << "3 weeks ago";
        QTest::newRow("months") << 100 * day << "3 months ago";
        QTest::newRow("year+months") << 400 * day << "1 year, 1 month ago";
        QTest::newRow("years") << 3650 * day << "10 years ago";
        QTest::newRow("future") << -hour << "";
    }

    void relativeTime() {
        QFETCH(qint64, ago);
        QFETCH(QString, text);
        const qint64 now = 1800000000;
        QCOMPARE(CommitModel::relativeTime(now - ago, now), text);
    }
};

QTEST_MAIN(TestCommitModel)
#include "tst_commitmodel.moc"
