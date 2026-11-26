#include "core/domain/ReleaseDate.h"

#include <QtTest>

using namespace semplice;

class TstReleaseDate : public QObject {
    Q_OBJECT
private slots:
    void validate_data()
    {
        QTest::addColumn<int>("year");
        QTest::addColumn<int>("month");
        QTest::addColumn<int>("day");
        QTest::addColumn<bool>("valid");

        QTest::newRow("year only") << 2020 << 0 << 0 << true;
        QTest::newRow("year+month") << 2020 << 5 << 0 << true;
        QTest::newRow("full date") << 2020 << 5 << 17 << true;
        QTest::newRow("oldest year") << 1744 << 0 << 0 << true;
        QTest::newRow("before oldest year") << 1743 << 0 << 0 << false;
        QTest::newRow("current year + 1") << 2027 << 12 << 31 << true;
        QTest::newRow("current year + 2") << 2028 << 0 << 0 << false;
        QTest::newRow("missing year") << 0 << 0 << 0 << false;
        QTest::newRow("day without month") << 2020 << 0 << 5 << false;
        QTest::newRow("month 13") << 2020 << 13 << 0 << false;
        QTest::newRow("negative month") << 2020 << -1 << 0 << false;
        QTest::newRow("Feb 30") << 2020 << 2 << 30 << false;
        QTest::newRow("Feb 29 leap year") << 2020 << 2 << 29 << true;
        QTest::newRow("Feb 29 other year") << 2021 << 2 << 29 << false;
        QTest::newRow("Apr 31") << 2020 << 4 << 31 << false;
        QTest::newRow("day 32") << 2020 << 1 << 32 << false;
    }

    void validate()
    {
        QFETCH(int, year);
        QFETCH(int, month);
        QFETCH(int, day);
        QFETCH(bool, valid);
        const auto r = ReleaseDate{year, month, day}.validate(2026);
        QCOMPARE(r.ok(), valid);
        if (!r.ok())
            QCOMPARE(r.error().code, ErrorCode::InvalidDate);
    }

    void missingMonthAndDaySortAsFirst()
    {
        QCOMPARE((ReleaseDate{2020, 0, 0}).sortKey(), 20200101);
        QCOMPARE((ReleaseDate{2020, 3, 0}).sortKey(), 20200301);
        QCOMPARE((ReleaseDate{2020, 3, 9}).sortKey(), 20200309);
        QVERIFY((ReleaseDate{2020, 0, 0}).sortKey() < (ReleaseDate{2020, 1, 2}).sortKey());
    }
};

QTEST_GUILESS_MAIN(TstReleaseDate)
#include "tst_releasedate.moc"
