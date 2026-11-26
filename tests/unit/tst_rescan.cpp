#include "SessionFixture.h"

using namespace semplice;
using namespace semplice::testing;

// M4: RescanService on a real library (SPEC 6.5).
class TstRescan : public QObject {
    Q_OBJECT

    static AlbumId importAlbum(SessionFixture& fx, const QString& artist, const QString& album, int count, int seed)
    {
        const QString folder = fx.albumFolder(album, artist, album, count, seed);
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        QVERIFY2_RETURN(scan.ok(), "scan failed", AlbumId());
        auto done = importer.commitAlbum(scan.value(), importer.propose(scan.value()).album);
        QVERIFY2_RETURN(done.ok(), "commit failed", AlbumId());
        return done.value().album;
    }

private slots:
    void aCleanLibraryStaysUntouched()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
        const QSet<QString> before = fx.filesOnDisk();
        auto r = fx.session->rescanner().run();
        QVERIFY(r.ok());
        QCOMPARE(r.value().checked, 3);
        QCOMPARE(r.value().refreshed, 0);
        QCOMPARE(r.value().adopted, 0);
        QCOMPARE(r.value().missingRemoved, 0);
        QCOMPARE(fx.filesOnDisk(), before);
        QCOMPARE(fx.verify(), QString());
    }

    void newFilesDroppedIntoTheFolderLandInARescanPlaylist()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        // a user copies two songs by hand into the library folder
        const QString a = fx.source(QStringLiteral("hand"), QStringLiteral("manual one"), 901, QStringLiteral("Manual One"),
                                    {QStringLiteral("Somebody")});
        const QString b = fx.source(QStringLiteral("hand"), QStringLiteral("manual two"), 902, QStringLiteral("Manual Two"),
                                    {QStringLiteral("Somebody feat. Friend")});
        QDir().mkpath(fx.abs(QStringLiteral("Artists/The Band/First")));
        QVERIFY(QFile::copy(a, fx.abs(QStringLiteral("Artists/The Band/First/manual one.mp3"))));
        QVERIFY(QFile::copy(b, fx.abs(QStringLiteral("Playlists/stray.mp3"))));

        auto r = fx.session->rescanner().run();
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol() + r.error().devMessage));
        QCOMPARE(r.value().adopted, 2);
        QVERIFY(r.value().rescanPlaylist.isValid());
        PlaylistRepo playlists(fx.session->database());
        const Playlist p = playlists.get(r.value().rescanPlaylist).value().value();
        QVERIFY(p.name.startsWith(QStringLiteral("Rescan 2026-10-19 12-00")));
        QCOMPARE(playlists.tracks(p.id).value().size(), 2);
        QVERIFY(!QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/First/manual one.mp3"))));  // moved into the playlist folder
        QVERIFY(fx.artistNamed(QStringLiteral("Friend")).has_value());
        QCOMPARE(fx.verify(), QString());

        // a second run finds nothing new
        auto again = fx.session->rescanner().run();
        QVERIFY(again.ok());
        QCOMPARE(again.value().adopted, 0);
        QCOMPARE(fx.verify(), QString());
    }

    void aStrayCopyOfALibraryTrackGoesToTheRecycleBin()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        const QString original = fx.abs(QStringLiteral("Artists/The Band/First/01 - First 1.mp3"));
        const QString copy = fx.abs(QStringLiteral("Artists/The Band/First/copy of one.mp3"));
        QVERIFY(QFile::copy(original, copy));
        // The library copy carries tags, so its bytes differ from the source. Use the very same bytes as a library
        // track by hash: the rescan hashes unknown files and compares with the stored (source) hash, so a stray
        // copy of the ORIGINAL SOURCE is the duplicate.
        const QString sourceCopy = fx.abs(QStringLiteral("Artists/The Band/First/source copy.mp3"));
        QVERIFY(QFile::copy(QDir(fx.srcDir()).filePath(QStringLiteral("First/t01.mp3")), sourceCopy));

        auto r = fx.session->rescanner().run();
        QVERIFY(r.ok());
        QCOMPARE(r.value().duplicatesTrashed, 1);
        QVERIFY(!QFileInfo::exists(sourceCopy));
        QCOMPARE(fx.trash.items().size(), 1);
        // the tagged copy is a different file (different bytes): it is adopted, not dropped
        QCOMPARE(r.value().adopted, 1);
        QCOMPARE(fx.verify(), QString());
    }

    void missingFilesAreRemovedWithTheirContainers()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("Second"), 3, 50);
        // the user deleted one song and a whole album folder in Explorer
        QVERIFY(QFile::remove(fx.abs(QStringLiteral("Artists/The Band/First/02 - First 2.mp3"))));
        QVERIFY(QDir(fx.abs(QStringLiteral("Artists/The Band/Second"))).removeRecursively());

        auto r = fx.session->rescanner().run();
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol() + r.error().devMessage));
        QVERIFY2(r.value().missingRemoved == 4, qPrintable(r.value().notes.join(QLatin1Char('|'))));
        QCOMPARE(r.value().missingBlocked, 0);
        AlbumRepo albums(fx.session->database());
        QCOMPARE(albums.tracks(album).value().size(), 2);
        QCOMPARE(fx.count(QStringLiteral("track")), 2);
        QCOMPARE(fx.verify(), QString());
    }

    void aVanishedDriveDoesNotWipeTheLibrary()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("Big"), 30, 10);
        for (int i = 1; i <= 29; ++i)
            QVERIFY(QFile::remove(fx.abs(QStringLiteral("Artists/The Band/Big/%1 - Big %2.mp3").arg(i, 2, 10, QLatin1Char('0')).arg(i))));

        auto r = fx.session->rescanner().run();
        QVERIFY(r.ok());
        QCOMPARE(r.value().missingBlocked, 29);
        QCOMPARE(r.value().missingRemoved, 0);
        QCOMPARE(fx.count(QStringLiteral("track")), 30);

        RescanOptions force;
        force.allowMassRemoval = true;
        auto forced = fx.session->rescanner().run(force);
        QVERIFY(forced.ok());
        QVERIFY2(forced.value().missingRemoved == 29, qPrintable(forced.value().notes.join(QLatin1Char('|'))));
        QCOMPARE(fx.count(QStringLiteral("track")), 1);
        QCOMPARE(fx.verify(), QString());
    }

    void aChangedFileIsRefreshedButKeepsItsData()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        AlbumRepo albums(fx.session->database());
        const TrackId t = albums.tracks(album).value().first();
        const Track before = fx.trackRow(t);
        const QString path = fx.abs(before.filePath);
        // another program rewrote the file (bigger)
        QFile f(path);
        QVERIFY(f.open(QIODevice::Append));
        f.write(QByteArray(500, '\0'));
        f.close();

        auto r = fx.session->rescanner().run();
        QVERIFY(r.ok());
        QCOMPARE(r.value().refreshed, 1);
        const Track after = fx.trackRow(t);
        QVERIFY(after.fileSize > before.fileSize);
        QCOMPARE(after.contentHash, before.contentHash);  // never recomputed
        QCOMPARE(after.title, before.title);
        QCOMPARE(fx.verify(), QString());
    }

    void unreadableFilesAreLeftWhereTheyAre()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        const QString junk = fx.abs(QStringLiteral("Artists/The Band/First/junk.flac"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        auto r = fx.session->rescanner().run();
        QVERIFY(r.ok());
        QCOMPARE(r.value().skipped, 1);
        QVERIFY(QFileInfo::exists(junk));
    }

    void cancellingARescanKeepsTheLibraryConsistent()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        for (int i = 0; i < 3; ++i) {
            const QString s = fx.source(QStringLiteral("hand"), QStringLiteral("m%1").arg(i), 950 + i, QStringLiteral("M%1").arg(i),
                                        {QStringLiteral("Somebody")});
            QVERIFY(QFile::copy(s, fx.abs(QStringLiteral("Playlists/m%1.mp3").arg(i))));
        }
        bool finished = false;
        for (int stopAt = 1; stopAt < 80 && !finished; ++stopAt) {
            std::stop_source source;
            int calls = 0;
            JobContext job;
            job.cancel = source.get_token();
            job.progress = [&](const JobProgress&) {
                if (++calls == stopAt)
                    source.request_stop();
            };
            auto r = fx.session->rescanner().run({}, job);
            QVERIFY(r.ok() || r.error().code == ErrorCode::Cancelled);
            if (r.ok() && !r.value().cancelled)
                finished = true;
            QCOMPARE(fx.verify(/*checkTags=*/false).count(QStringLiteral("row without file")), 0);
        }
        QVERIFY(finished);
        QCOMPARE(fx.verify(), QString());
        QCOMPARE(fx.count(QStringLiteral("track")), 5);
    }

    void rescanBacksUpAndOptimisesTheDatabase()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        auto r = fx.session->rescanner().run();
        QVERIFY(r.ok());
        QVERIFY(r.value().notes.isEmpty());
        QVERIFY(QFileInfo::exists(fx.session->layout().backupPath(1)));
    }
};

QTEST_GUILESS_MAIN(TstRescan)
#include "tst_rescan.moc"
