#include "core/db/Database.h"
#include "core/db/Migrator.h"

#include <QtTest>

using namespace semplice;

class TstMigrator : public QObject {
    Q_OBJECT
private slots:
    void freshDatabaseGetsTheLatestSchema()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QCOMPARE(Migrator::currentVersion(*db).value(), 0);
        QVERIFY(Migrator::migrate(*db).ok());
        QCOMPARE(Migrator::currentVersion(*db).value(), Migrator::latestVersion());
        QVERIFY(Migrator::latestVersion() >= 1);

        for (const char* table : {"artist", "album", "album_artist", "track", "album_track", "playlist",
                                  "playlist_track", "track_artist", "artist_tag", "album_tag", "track_tag",
                                  "recent_item", "kv"}) {
            const auto n = db->scalar(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?"),
                                      {QString::fromLatin1(table)});
            QCOMPARE(n.value().toInt(), 1);
        }
    }

    void migratingTwiceChangesNothing()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        QVERIFY(db->exec(QStringLiteral("INSERT INTO kv(key, value) VALUES('x','y')")).ok());
        QVERIFY(Migrator::migrate(*db).ok());
        QCOMPARE(db->scalar(QStringLiteral("SELECT value FROM kv WHERE key='x'")).value().toString(), QStringLiteral("y"));
    }

    void refusesADatabaseFromANewerVersion()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(db->exec(QStringLiteral("PRAGMA user_version = %1").arg(Migrator::latestVersion() + 1)).ok());
        const auto r = Migrator::migrate(*db);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::SchemaTooNew);
    }

    void foreignKeysAndWalAreOn()
    {
        QTemporaryDir dir;
        auto db = Database::open(dir.filePath(QStringLiteral("lib.db"))).value();
        QCOMPARE(db->scalar(QStringLiteral("PRAGMA foreign_keys")).value().toInt(), 1);
        QCOMPARE(db->scalar(QStringLiteral("PRAGMA journal_mode")).value().toString().toLower(), QStringLiteral("wal"));
    }

    void aFailedMigrationLeavesTheVersionUntouched()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(db->exec(QStringLiteral("CREATE TABLE artist(x)")).ok());  // 0001 will collide with this
        QVERIFY(!Migrator::migrate(*db).ok());
        QCOMPARE(Migrator::currentVersion(*db).value(), 0);
        // the half-applied migration was rolled back: only our own table exists
        QCOMPARE(db->scalar(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='table'")).value().toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(TstMigrator)
#include "tst_migrator.moc"
