#include "core/storage/ThumbnailCache.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace semplice;

class TstThumbnailCache : public QObject {
    Q_OBJECT

    static void make(const QString& dir, const QString& name, int bytes, const QDateTime& when)
    {
        QFile f(QDir(dir).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(bytes, 'x'));
        QVERIFY(f.setFileTime(when, QFileDevice::FileModificationTime));
    }

private slots:
    void underTheCapNothingIsRemoved()
    {
        QTemporaryDir dir;
        make(dir.path(), "a.png", 100, QDateTime::currentDateTime());
        QCOMPARE(ThumbnailCache::trim(dir.path(), 1000), 0);
    }
    void overTheCapTheOldestGoFirstDownToEightyPercent()
    {
        QTemporaryDir dir;
        const QDateTime now = QDateTime::currentDateTime();
        for (int i = 0; i < 10; ++i)
            make(dir.path(), QStringLiteral("f%1.png").arg(i), 100, now.addDays(-10 + i));  // f0 is the oldest
        QCOMPARE(ThumbnailCache::trim(dir.path(), 900), 3);  // 1000 -> 700 <= 720
        QVERIFY(!QFile::exists(dir.filePath("f0.png")));
        QVERIFY(!QFile::exists(dir.filePath("f2.png")));
        QVERIFY(QFile::exists(dir.filePath("f3.png")));
        QVERIFY(QFile::exists(dir.filePath("f9.png")));
    }
    void touchProtectsAFileFromTheNextTrim()
    {
        QTemporaryDir dir;
        const QDateTime now = QDateTime::currentDateTime();
        make(dir.path(), "old.png", 500, now.addDays(-30));
        make(dir.path(), "newer.png", 500, now.addDays(-2));
        ThumbnailCache::touch(dir.filePath("old.png"));
        QCOMPARE(ThumbnailCache::trim(dir.path(), 900), 1);
        QVERIFY(QFile::exists(dir.filePath("old.png")));
        QVERIFY(!QFile::exists(dir.filePath("newer.png")));
    }
    void aMissingFolderIsFine() { QCOMPARE(ThumbnailCache::trim(QStringLiteral("Z:/does/not/exist")), 0); }
};

QTEST_MAIN(TstThumbnailCache)
#include "tst_thumbnail_cache.moc"
