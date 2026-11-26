#include "ui/CoverImageProvider.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace semplice;

// The pictures of the library come back cut square and masked, so the QML needs no mask effect per picture.
class TstCoverProvider : public QObject {
    Q_OBJECT
private slots:
    void aCircleIsTransparentInTheCornersAndOpaqueInTheMiddle()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage wide(400, 200, QImage::Format_RGB32);
        wide.fill(Qt::red);
        QVERIFY(wide.save(dir.filePath("cover.png")));

        CoverImageProvider provider;
        provider.setLibraryRoot(dir.path());
        const QImage circle = provider.load(QStringLiteral("cover.png?shape=circle&r=0.2"), QSize(64, 64));
        QCOMPARE(circle.size(), QSize(64, 64));  // cut square, scaled to the requested side
        QCOMPARE(qAlpha(circle.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(circle.pixel(63, 63)), 0);
        QCOMPARE(qAlpha(circle.pixel(32, 32)), 255);
        QCOMPARE(qRed(circle.pixel(32, 32)), 255);
    }

    void aRoundedSquareKeepsItsEdgesAndLosesItsCorners()
    {
        QTemporaryDir dir;
        QImage square(100, 100, QImage::Format_RGB32);
        square.fill(Qt::blue);
        QVERIFY(square.save(dir.filePath("a.png")));

        CoverImageProvider provider;
        provider.setLibraryRoot(dir.path());
        const QImage rounded = provider.load(QStringLiteral("a.png?shape=rounded&r=0.3"), QSize(100, 100));
        QCOMPARE(qAlpha(rounded.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(rounded.pixel(50, 1)), 255);
        QCOMPARE(qAlpha(rounded.pixel(50, 50)), 255);
    }

    void withoutAShapeThePictureIsUntouched()
    {
        QTemporaryDir dir;
        QImage wide(400, 200, QImage::Format_RGB32);
        wide.fill(Qt::green);
        QVERIFY(wide.save(dir.filePath("c.png")));

        CoverImageProvider provider;
        provider.setLibraryRoot(dir.path());
        const QImage plain = provider.load(QStringLiteral("c.png"), QSize(100, 100));
        QCOMPARE(plain.width(), 200);  // the longer side wins (the view crops)
        QCOMPARE(qAlpha(plain.pixel(0, 0)), 255);
    }
};

QTEST_MAIN(TstCoverProvider)
#include "tst_cover_provider.moc"
