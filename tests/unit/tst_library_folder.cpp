#include "StorageTestSupport.h"

#include "core/db/Migrator.h"
#include "core/storage/DbCommitLog.h"
#include "core/storage/LibraryFolder.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace semplice;
using namespace semplice::testing;

namespace {

class RecordingAttributes final : public IFileAttributes {
public:
    Result<void> setHidden(const QString& path) override
    {
        hidden << path;
        return {};
    }
    QStringList hidden;
};

// A clock the test can move.
class MovableClock final : public IClock {
public:
    QDateTime now() const override { return m_now; }
    void set(const QDateTime& t) { m_now = t; }
    void advanceHours(int h) { m_now = m_now.addSecs(3600 * h); }

private:
    QDateTime m_now = QDateTime(QDate(2026, 10, 19), QTime(12, 0), QTimeZone::UTC);
};

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

void writeJson(const QString& path, const QJsonObject& o)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(o).toJson());
}

// Reads a value from a backup / database file without modifying it.
QString kvValue(const QString& dbFile, const QString& key)
{
    auto db = Database::open(dbFile, true);
    if (!db.ok())
        return QStringLiteral("<cannot open>");
    auto v = db.value()->scalar(QStringLiteral("SELECT value FROM kv WHERE key = ?"), {key});
    return v.ok() && v.value().isValid() ? v.value().toString() : QString();
}

}  // namespace

class TstLibraryFolder : public QObject {
    Q_OBJECT

private slots:
    // ================================================================ create
    void createMakesTheLayoutTheMarkerAndTheDatabase()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(QStringLiteral("My Music"));
        FakeTrash trash(dir.filePath(QStringLiteral("trash")));
        MovableClock clock;
        RecordingAttributes attributes;

        auto lib = createLibrary(root, attributes, trash, clock);
        QVERIFY2(lib.ok(), lib.ok() ? "" : qPrintable(lib.error().devMessage));
        const LibraryLayout& l = lib.value().layout;
        for (const QString& p : {l.dotLibrary, l.journalDir, l.coversDir, l.artistsDir, l.playlistsDir})
            QVERIFY2(QFileInfo(p).isDir(), qPrintable(p));
        QVERIFY(QFileInfo(l.dbPath).isFile());
        QCOMPARE(attributes.hidden, QStringList{l.dotLibrary});  // .library is hidden

        const QJsonObject json = readJson(l.jsonPath);
        QCOMPARE(json.value(QStringLiteral("app")).toString(), QStringLiteral("semplice"));
        QVERIFY(!json.value(QStringLiteral("library_uid")).toString().isEmpty());
        QCOMPARE(json.value(QStringLiteral("schema")).toInt(), Migrator::latestVersion());
        QCOMPARE(lib.value().info.uid, json.value(QStringLiteral("library_uid")).toString());

        QCOMPARE(Migrator::currentVersion(*lib.value().db).value(), Migrator::latestVersion());
        QVERIFY(lib.value().backupCreated);  // first start: backup right away
        QVERIFY(QFileInfo(l.backupPath(1)).isFile());
        QVERIFY(!lib.value().recovery.repairedAnything());
    }

    void createRefusesAFolderThatIsNotEmpty()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("holiday.jpg")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        FakeTrash trash(dir.filePath(QStringLiteral("trash-sandbox")));
        MovableClock clock;
        NullFileAttributes attributes;
        const auto lib = createLibrary(dir.path(), attributes, trash, clock);
        QVERIFY(!lib.ok());
        QCOMPARE(lib.error().code, ErrorCode::FolderNotEmpty);
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral(".library"))));  // nothing was created
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("holiday.jpg"))));
    }

    void createAcceptsAnEmptyExistingFolder()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash fake(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        QVERIFY(createLibrary(dir.path(), attributes, fake, clock).ok());
    }

    // ================================================================ open / inspect
    void openFindsTheSameLibraryAgain()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        QString uid;
        {
            auto created = createLibrary(dir.path(), attributes, trash, clock);
            QVERIFY(created.ok());
            uid = created.value().info.uid;
            QVERIFY(created.value().db->exec(QStringLiteral("INSERT INTO kv(key,value) VALUES('k','v')")).ok());
        }
        auto opened = openLibrary(dir.path(), trash, clock);
        QVERIFY2(opened.ok(), opened.ok() ? "" : qPrintable(opened.error().devMessage));
        QCOMPARE(opened.value().info.uid, uid);
        QCOMPARE(opened.value().db->scalar(QStringLiteral("SELECT value FROM kv WHERE key='k'")).value().toString(), QStringLiteral("v"));
        QVERIFY(!opened.value().backupCreated);  // the first backup is still fresh
    }

    void foldersThatAreNotLibrariesAreRefused()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;

        QCOMPARE(openLibrary(dir.path(), trash, clock).error().code, ErrorCode::NotALibrary);  // empty folder
        QCOMPARE(openLibrary(dir.filePath(QStringLiteral("missing")), trash, clock).error().code, ErrorCode::NotALibrary);

        QDir().mkpath(dir.filePath(QStringLiteral(".library")));
        writeJson(dir.filePath(QStringLiteral(".library/library.json")),
                  {{QStringLiteral("app"), QStringLiteral("some-other-player")}, {QStringLiteral("library_uid"), QStringLiteral("x")}, {QStringLiteral("schema"), 1}});
        QCOMPARE(openLibrary(dir.path(), trash, clock).error().code, ErrorCode::NotALibrary);  // foreign marker

        QFile broken(dir.filePath(QStringLiteral(".library/library.json")));
        QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
        broken.write("{ this is not json");
        broken.close();
        QCOMPARE(openLibrary(dir.path(), trash, clock).error().code, ErrorCode::NotALibrary);

        writeJson(dir.filePath(QStringLiteral(".library/library.json")),
                  {{QStringLiteral("app"), QStringLiteral("semplice")}, {QStringLiteral("library_uid"), QStringLiteral("x")}, {QStringLiteral("schema"), 1}});
        QCOMPARE(openLibrary(dir.path(), trash, clock).error().code, ErrorCode::NotALibrary);  // marker but no database
    }

    void aLibraryFromANewerSemplicePerJsonIsRefused()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        LibraryLayout layout;
        {
            auto created = createLibrary(dir.path(), attributes, trash, clock);
            QVERIFY(created.ok());
            layout = created.value().layout;
        }
        QJsonObject json = readJson(layout.jsonPath);
        json.insert(QStringLiteral("schema"), Migrator::latestVersion() + 1);
        writeJson(layout.jsonPath, json);
        const auto r = openLibrary(dir.path(), trash, clock);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::SchemaTooNew);
    }

    void aDatabaseFromANewerSempliceIsRefusedAndLeftAlone()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        LibraryLayout layout;
        {
            auto created = createLibrary(dir.path(), attributes, trash, clock);
            QVERIFY(created.ok());
            layout = created.value().layout;
            QVERIFY(created.value().db->exec(QStringLiteral("PRAGMA user_version = %1").arg(Migrator::latestVersion() + 3)).ok());
        }
        const auto r = openLibrary(dir.path(), trash, clock);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::SchemaTooNew);
        auto db = Database::open(layout.dbPath, true);
        QCOMPARE(Migrator::currentVersion(*db.value()).value(), Migrator::latestVersion() + 3);  // untouched
    }

    // ================================================================ backups (SPEC 6.7)
    void backupsRotateAfter24HoursAndKeepThree()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        LibraryLayout layout;
        auto setGeneration = [&](int generation) {
            auto lib = openLibrary(dir.path(), trash, clock);
            QVERIFY(lib.ok());
            QVERIFY(lib.value().db->exec(QStringLiteral("INSERT OR REPLACE INTO kv(key,value) VALUES('gen',?)"),
                                          {QString::number(generation)}).ok());
        };
        {
            auto created = createLibrary(dir.path(), attributes, trash, clock);
            QVERIFY(created.ok());
            layout = created.value().layout;
        }
        QCOMPARE(kvValue(layout.backupPath(1), QStringLiteral("gen")), QString());  // bak1 from creation: no data yet

        // Within 24 hours nothing new is written.
        clock.advanceHours(23);
        {
            auto lib = openLibrary(dir.path(), trash, clock);
            QVERIFY(lib.ok());
            QVERIFY(!lib.value().backupCreated);
        }

        for (int generation = 1; generation <= 4; ++generation) {
            setGeneration(generation);
            clock.advanceHours(25);
            auto lib = openLibrary(dir.path(), trash, clock);
            QVERIFY(lib.ok());
            QVERIFY2(lib.value().backupCreated, qPrintable(QString::number(generation)));
        }
        // newest first: gen 4, 3, 2 ; generation 1 and the empty first backup fell off the end
        QCOMPARE(kvValue(layout.backupPath(1), QStringLiteral("gen")), QStringLiteral("4"));
        QCOMPARE(kvValue(layout.backupPath(2), QStringLiteral("gen")), QStringLiteral("3"));
        QCOMPARE(kvValue(layout.backupPath(3), QStringLiteral("gen")), QStringLiteral("2"));
        QVERIFY(!QFileInfo::exists(layout.backupPath(4)));
        QVERIFY(!QFileInfo::exists(layout.dbPath + QStringLiteral(".bak-new")));  // no temporary file left
        QCOMPARE(Migrator::currentVersion(*Database::open(layout.backupPath(1), true).value()).value(), Migrator::latestVersion());
    }

    // ================================================================ health check and restore
    void aDamagedDatabaseIsDetectedAndCanBeRestoredFromTheNewestGoodBackup()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        LibraryLayout layout;
        {
            auto created = createLibrary(dir.path(), attributes, trash, clock);
            QVERIFY(created.ok());
            layout = created.value().layout;
            // enough data to spread over many pages, written AFTER the first backup was made
            Transaction tx(*created.value().db);
            QVERIFY(tx.begin().ok());
            for (int i = 0; i < 4000; ++i)
                QVERIFY(created.value().db->exec(QStringLiteral("INSERT INTO kv(key,value) VALUES(?,?)"),
                                                 {QStringLiteral("key%1").arg(i), QString(150, QLatin1Char('x'))}).ok());
            QVERIFY(tx.commit().ok());
        }
        // Flip a block in the middle of the file.
        {
            QFile f(layout.dbPath);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.size() > 300000);
            f.seek(80000);
            f.write(QByteArray(120000, '\xFF'));
        }
        const auto broken = openLibrary(dir.path(), trash, clock);
        QVERIFY(!broken.ok());
        QCOMPARE(broken.error().code, ErrorCode::DatabaseCorrupt);

        const auto restored = LibraryFolder::restoreNewestGoodBackup(layout, clock);
        QVERIFY2(restored.ok(), restored.ok() ? "" : qPrintable(restored.error().devMessage));
        QCOMPARE(restored.value(), 1);
        // the damaged file is kept, not deleted
        QCOMPARE(QDir(layout.dotLibrary).entryList({QStringLiteral("library.db.corrupt-*")}, QDir::Files).size(), 1);

        const auto reopened = openLibrary(dir.path(), trash, clock);
        QVERIFY2(reopened.ok(), reopened.ok() ? "" : qPrintable(reopened.error().devMessage));
    }

    void restoreSkipsABackupThatIsDamagedToo()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        LibraryLayout layout;
        {
            auto created = createLibrary(dir.path(), attributes, trash, clock);
            QVERIFY(created.ok());
            layout = created.value().layout;
            QVERIFY(created.value().db->exec(QStringLiteral("INSERT INTO kv(key,value) VALUES('old','1')")).ok());
        }
        clock.advanceHours(30);
        {
            auto second = openLibrary(dir.path(), trash, clock);  // makes bak1 (with 'old'), bak2 = the empty one
            QVERIFY(second.ok());
            QVERIFY(second.value().backupCreated);
        }
        QFile bak1(layout.backupPath(1));
        QVERIFY(bak1.open(QIODevice::WriteOnly | QIODevice::Truncate));
        bak1.write("not a database at all, just text");
        bak1.close();
        QFile db(layout.dbPath);
        QVERIFY(db.open(QIODevice::WriteOnly | QIODevice::Truncate));
        db.write("also garbage");
        db.close();

        const auto restored = LibraryFolder::restoreNewestGoodBackup(layout, clock);
        QVERIFY(restored.ok());
        QCOMPARE(restored.value(), 2);  // bak1 was unusable
        QVERIFY(openLibrary(dir.path(), trash, clock).ok());
    }

    void restoreFailsClearlyWhenNoBackupIsUsable()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        LibraryLayout layout;
        {
            auto created = createLibrary(dir.path(), attributes, trash, clock);
            QVERIFY(created.ok());
            layout = created.value().layout;
        }
        QFile::remove(layout.backupPath(1));  // test cleanup only
        const auto r = LibraryFolder::restoreNewestGoodBackup(layout, clock);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::DatabaseCorrupt);
        QVERIFY(QFileInfo(layout.dbPath).isFile());  // the current file was not touched
    }

    // ================================================================ journal recovery at open
    void pendingOperationsAreRepairedBeforeTheLibraryIsHandedOver()
    {
        QTemporaryDir dir;
        QTemporaryDir sandbox;
        FakeTrash trash(sandbox.path());
        MovableClock clock;
        NullFileAttributes attributes;
        QString committedId;
        QString lostId;
        QDir().mkpath(dir.path());
        {
            auto lib = createLibrary(dir.path(), attributes, trash, clock);
            QVERIFY(lib.ok());
            FileStorageService& storage = *lib.value().storage;
            Database& db = *lib.value().db;
            const QString src = sandbox.filePath(QStringLiteral("a.mp3"));
            QFile f(src);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("audio");
            f.close();

            // Operation 1: files done, DB committed, power lost before finish() -> must roll forward.
            QDir().mkpath(dir.filePath(QStringLiteral("Artists/Old")));
            QFile old(dir.filePath(QStringLiteral("Artists/Old/gone.mp3")));
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write("old");
            old.close();
            auto op1 = storage.begin({{FileOp::Kind::Copy, src, QStringLiteral("Artists/New/a.mp3")},
                                      {FileOp::Kind::Trash, QStringLiteral("Artists/Old/gone.mp3"), {}}});
            QVERIFY(op1.ok());
            QVERIFY(op1.value()->execute().ok());
            committedId = op1.value()->id();
            {
                Transaction tx(db);
                QVERIFY(tx.begin().ok());
                QVERIFY(DbCommitLog::mark(db, committedId, 1).ok());
                QVERIFY(tx.commit().ok());
            }
            // Operation 2: files done, DB never committed -> must roll back.
            auto op2 = storage.begin({{FileOp::Kind::Copy, src, QStringLiteral("Artists/Lost/a.mp3")}});
            QVERIFY(op2.ok());
            QVERIFY(op2.value()->execute().ok());
            lostId = op2.value()->id();
            QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("Artists/Lost/a.mp3"))));
        }  // "crash": nothing finished

        auto reopened = openLibrary(dir.path(), trash, clock);
        QVERIFY2(reopened.ok(), reopened.ok() ? "" : qPrintable(reopened.error().devMessage));
        const RecoveryReport& report = reopened.value().recovery;
        QCOMPARE(report.rolledForward, 1);
        QCOMPARE(report.rolledBack, 1);
        QVERIFY(report.problems.isEmpty());
        QVERIFY(report.repairedAnything());  // -> the app shows one info banner

        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("Artists/New/a.mp3"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("Artists/Old/gone.mp3"))));
        QVERIFY(QFileInfo(trash.sandbox()).exists());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("Artists/Lost"))));  // copy and its new folder are gone
        QCOMPARE(QDir(reopened.value().layout.journalDir).entryList({QStringLiteral("*.json")}).size(), 0);
        // the commit log was cleaned up too
        QCOMPARE(reopened.value().db->scalar(QStringLiteral("SELECT COUNT(*) FROM op_log")).value().toInt(), 0);
        Q_UNUSED(lostId);
    }
};

QTEST_GUILESS_MAIN(TstLibraryFolder)
#include "tst_library_folder.moc"
