#include "StorageTestSupport.h"
#include "TestLibrary.h"

#include <QDirIterator>

using namespace semplice;
using namespace semplice::testing;

// M1 (rules) and M2 (storage) together: LibraryService operations executed with the REAL
// FileStorageService on a real folder. Afterwards the files on disk must be exactly the files the
// database says exist - no more, no fewer.
class TstStorageIntegration : public QObject {
    Q_OBJECT

    // The new tracks of a freshly created album have no audio file yet (the importer of M4 copies
    // them); write a stand-in file for every track so later moves and deletions have something to act on.
    static void materialise(TestLibrary& lib, StorageEnv& env)
    {
        auto rows = lib.db->run(QStringLiteral("SELECT file_path, title FROM track"));
        QVERIFY(rows.ok());
        QSqlQuery q = std::move(rows).value();
        while (q.next())
            env.writeLib(q.value(0).toString(), q.value(1).toString().toUtf8());
    }

    static QSet<QString> filesOnDisk(const StorageEnv& env)
    {
        QSet<QString> out;
        for (const QString& top : {QStringLiteral("Artists"), QStringLiteral("Playlists")}) {
            const QString base = env.lib(top);
            if (!QFileInfo::exists(base))
                continue;
            QDirIterator it(base, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext())
                out.insert(QDir(env.root).relativeFilePath(it.next()).toLower());
        }
        return out;
    }

    static QSet<QString> filesInDatabase(TestLibrary& lib)
    {
        QSet<QString> out;
        auto rows = lib.db->run(QStringLiteral("SELECT file_path FROM track"));
        QSqlQuery q = std::move(rows).value();
        while (q.next())
            out.insert(q.value(0).toString().toLower());
        return out;
    }

    // The core assertion of this test file.
    static void verifyDiskMatchesDatabase(TestLibrary& lib, StorageEnv& env, const char* what)
    {
        QCOMPARE(lib.integrity(), QString());
        const QSet<QString> db = filesInDatabase(lib);
        const QSet<QString> disk = filesOnDisk(env);
        QSet<QString> missing = db - disk;
        QSet<QString> stray = disk - db;
        QVERIFY2(missing.isEmpty() && stray.isEmpty(),
                 qPrintable(QStringLiteral("%1\n missing on disk: %2\n not in database: %3")
                                .arg(QLatin1String(what), QStringList(missing.begin(), missing.end()).join(QLatin1Char('|')),
                                     QStringList(stray.begin(), stray.end()).join(QLatin1Char('|')))));
    }

private slots:
    void homeMoveAndRenumberingOnTheRealFilesystem()
    {
        StorageEnv env;
        TestLibrary lib(env.storage.get());
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId one = lib.album(a, QStringLiteral("One"), 4);
        const AlbumId two = lib.album(a, QStringLiteral("Two"), 1);
        const TrackId t = lib.trackAt(one, 2);
        lib.clock.advance(1000);
        QVERIFY(lib.service->addTrackToAlbum(t, two).ok());
        materialise(lib, env);
        verifyDiskMatchesDatabase(lib, env, "before");

        QVERIFY(lib.service->removeTrackFromAlbum(t, one).ok());
        verifyDiskMatchesDatabase(lib, env, "after removeTrackFromAlbum");
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/A/Two/02 - One track 2.mp3"))));
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/A/One/02 - One track 3.mp3"))));  // renumbered
        QCOMPARE(env.storage->pendingOperationIds().size(), 0);
    }

    void deletingAnAlbumSendsOnlyUnusedTracksToTheRecycleBin()
    {
        StorageEnv env;
        TestLibrary lib(env.storage.get());
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId doomed = lib.album(a, QStringLiteral("Doomed"), 3);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("Keeper"), a, 1);
        const TrackId kept = lib.trackAt(doomed, 2);
        QVERIFY(lib.service->addTrackToPlaylist(kept, p).ok());
        materialise(lib, env);

        QVERIFY(lib.service->deleteAlbum(doomed).ok());
        verifyDiskMatchesDatabase(lib, env, "after deleteAlbum");
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Playlists/Keeper/Doomed track 2 - A.mp3"))));
        const TreeSnapshot bin = env.trashTree();
        QCOMPARE(bin.files.value(QStringLiteral("01 - Doomed track 1.mp3")), QByteArray("Doomed track 1"));
        QCOMPARE(bin.files.value(QStringLiteral("03 - Doomed track 3.mp3")), QByteArray("Doomed track 3"));
        QVERIFY(!bin.files.contains(QStringLiteral("Doomed track 2 - A.mp3")));  // the kept track was NOT trashed
    }

    void rehomingACollaborationMovesTheFolder()
    {
        StorageEnv env;
        TestLibrary lib(env.storage.get());
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId b = lib.artist(QStringLiteral("B"));
        const AlbumId collab = lib.album(a, QStringLiteral("Together"), 2, {b});
        lib.album(b, QStringLiteral("Hits"), 1);
        lib.album(a, QStringLiteral("Solo"), 2);
        materialise(lib, env);
        Q_UNUSED(collab);

        QVERIFY(lib.service->deleteArtist(a).ok());
        verifyDiskMatchesDatabase(lib, env, "after deleteArtist(primary of a collaboration)");
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/B/Together/01 - Together track 1.mp3"))));
        QVERIFY(!QFileInfo::exists(env.lib(QStringLiteral("Artists/A"))));  // the hidden artist's folder is gone (Recycle Bin)
        QVERIFY(env.trashTree().files.contains(QStringLiteral("A/Solo/01 - Solo track 1.mp3")) ||
                !env.trashTree().files.isEmpty());
    }

    void addToDiscographyMovesThePlaylistFileIntoTheUnsortedFolder()
    {
        StorageEnv env;
        TestLibrary lib(env.storage.get());
        const ArtistId hidden = lib.artist(QStringLiteral("H"), true);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("Mix"), hidden, 2);
        materialise(lib, env);

        QVERIFY(lib.service->materialiseHiddenArtist(hidden).ok());
        verifyDiskMatchesDatabase(lib, env, "after materialiseHiddenArtist");
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/H/Unsorted tracks/01 - Mix song 1.mp3"))));
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/H/Unsorted tracks/02 - Mix song 2.mp3"))));
        Q_UNUSED(p);

    }

    void losingTheLastAlbumDissolvesUnsortedOnDisk()
    {
        StorageEnv env;
        TestLibrary lib(env.storage.get());
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId real = lib.album(a, QStringLiteral("Only"), 2);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        materialise(lib, env);
        QVERIFY(lib.service->addToDiscography(lib.trackAt(p, 1), a).ok());
        verifyDiskMatchesDatabase(lib, env, "after addToDiscography");

        QVERIFY(lib.service->deleteAlbum(real).ok());
        verifyDiskMatchesDatabase(lib, env, "after deleting the last real album");
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Playlists/P/P song 1 - A.mp3"))));
        QVERIFY(!QFileInfo::exists(env.lib(QStringLiteral("Artists/A/Unsorted tracks"))));
        QVERIFY(lib.artistRow(a).hidden);
    }

    void deletingATrackInTheMiddleRenumbersTheFilesThatFollow()
    {
        StorageEnv env;
        TestLibrary lib(env.storage.get());
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("Five"), 5);
        materialise(lib, env);
        QVERIFY(lib.service->deleteTrack(lib.trackAt(album, 2)).ok());
        verifyDiskMatchesDatabase(lib, env, "after deleteTrack");
        for (int i = 1; i <= 4; ++i) {
            const QString prefix = QStringLiteral("%1 - ").arg(i, 2, 10, QLatin1Char('0'));
            QVERIFY(!QDir(env.lib(QStringLiteral("Artists/A/Five"))).entryList({prefix + QStringLiteral("*")}).isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TstStorageIntegration)
#include "tst_storage_integration.moc"
