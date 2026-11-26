#include "core/util/WindowGeometryPolicy.h"

#include <QtTest>

using namespace semplice;

class TstWindowGeometry : public QObject {
    Q_OBJECT
    const QSize kMin{1000, 620};
    const QSize kDefault{1280, 760};
    const QList<QRect> kOneScreen{QRect(0, 0, 1920, 1040)};
    const QList<QRect> kTwoScreens{QRect(0, 0, 1920, 1040), QRect(1920, 0, 1920, 1040)};

private slots:
    void nothingSavedGivesCentredDefault()
    {
        const QRect r = resolveWindowRect({}, kOneScreen, kMin, kDefault);
        QCOMPARE(r.size(), kDefault);
        QCOMPARE(r.center(), kOneScreen.first().center());
    }

    void visibleSavedRectIsKept()
    {
        const WindowGeometry saved{100, 80, 1200, 700, false, true};
        QCOMPARE(resolveWindowRect(saved, kOneScreen, kMin, kDefault), QRect(100, 80, 1200, 700));
    }

    void rectOnSecondMonitorIsKeptWhenMonitorExists()
    {
        const WindowGeometry saved{2000, 50, 1200, 700, false, true};
        QCOMPARE(resolveWindowRect(saved, kTwoScreens, kMin, kDefault), QRect(2000, 50, 1200, 700));
    }

    void rectOnMissingMonitorFallsBackToPrimary()
    {
        const WindowGeometry saved{2000, 50, 1200, 700, false, true};
        const QRect r = resolveWindowRect(saved, kOneScreen, kMin, kDefault);
        QCOMPARE(r.size(), kDefault);
        QVERIFY(kOneScreen.first().contains(r));
    }

    void barelyVisibleRectFallsBack()
    {
        // Only 20 px of the window overlaps the screen: not reachable.
        const WindowGeometry saved{1900, 10, 1200, 700, false, true};
        const QRect r = resolveWindowRect(saved, kOneScreen, kMin, kDefault);
        QCOMPARE(r.size(), kDefault);
    }

    void tooSmallSizeIsRaisedToMinimum()
    {
        const WindowGeometry saved{100, 80, 300, 200, false, true};
        const QRect r = resolveWindowRect(saved, kOneScreen, kMin, kDefault);
        QCOMPARE(r.size(), kMin);
        QCOMPARE(r.topLeft(), QPoint(100, 80));
    }

    void defaultLargerThanScreenIsClamped()
    {
        const QList<QRect> small{QRect(0, 0, 1024, 700)};
        const QRect r = resolveWindowRect({}, small, kMin, kDefault);
        QCOMPARE(r.size(), QSize(1024, 700));
    }
};

QTEST_GUILESS_MAIN(TstWindowGeometry)
#include "tst_windowgeometry.moc"
