#include "core/util/Result.h"

#include <QtTest>

using namespace semplice;

class TstResult : public QObject {
    Q_OBJECT
private slots:
    void valueResult()
    {
        const Result<int> r = 42;
        QVERIFY(r.ok());
        QVERIFY(static_cast<bool>(r));
        QCOMPARE(r.value(), 42);
        QCOMPARE(r.valueOr(7), 42);
    }

    void errorResult()
    {
        const Result<int> r = Error::make(ErrorCode::NotFound, QStringLiteral("no such row"));
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::NotFound);
        QCOMPARE(r.error().devMessage, QStringLiteral("no such row"));
        QCOMPARE(r.valueOr(7), 7);
    }

    void voidResult()
    {
        const Result<void> good;
        QVERIFY(good.ok());
        const Result<void> bad = Error::make(ErrorCode::IoError);
        QVERIFY(!bad.ok());
        QCOMPARE(bad.error().code, ErrorCode::IoError);
    }

    void moveOnlyValue()
    {
        Result<QString> r = QStringLiteral("abc");
        QCOMPARE(std::move(r).value(), QStringLiteral("abc"));
    }

    void errorRegistry()
    {
        const Error tooLong = Error::make(ErrorCode::PathTooLong);
        QCOMPARE(tooLong.symbol(), QStringLiteral("E_PATH_TOO_LONG"));
        QCOMPARE(tooLong.numericCode(), 100);
        QVERIFY(tooLong.hasUserMessage());

        // An error without a user message falls back to the generic "Error -1" banner.
        const Error unknown;
        QCOMPARE(unknown.numericCode(), -1);
        QCOMPARE(unknown.symbol(), QStringLiteral("E_UNKNOWN"));
        QVERIFY(!unknown.hasUserMessage());
    }
};

QTEST_GUILESS_MAIN(TstResult)
#include "tst_result.moc"
