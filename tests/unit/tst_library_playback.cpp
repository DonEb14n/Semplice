#include "PlaybackFakes.h"
#include "SessionFixture.h"

#include "core/playback/LibraryPlayback.h"
#include "core/playback/PlaybackService.h"
#include "core/recommend/LibraryRecommendationSource.h"
#include "core/storage/LibraryFolder.h"

using namespace semplice;
using namespace semplice::testing;

// M5: the playback service on a real library (real database, real files, fake audio device).
class TstLibraryPlayback : public QObject {
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
    void theCatalogDescribesTracksAndNoticesMissingFiles()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        LibraryTrackCatalog catalog(fx.session->database(), fx.libRoot());
        AlbumRepo albums(fx.session->database());
        const TrackId t = albums.tracks(album).value().first();
        const auto info = catalog.resolve(t);
        QVERIFY(info.has_value());
        QCOMPARE(info->title, QStringLiteral("First 1"));
        QCOMPARE(info->artist, QStringLiteral("The Band"));
        QCOMPARE(info->albumTitle, QStringLiteral("First"));
        QVERIFY(QFileInfo(info->absolutePath).isFile());
        QVERIFY(info->durationMs >= 0);

        QVERIFY(QFile::remove(info->absolutePath));  // deleted behind our back
        QVERIFY(!catalog.resolve(t).has_value());
        QVERIFY(!catalog.resolve(TrackId(9999)).has_value());
    }

    void theRealRecommendationSourceSuggestsTracksOutsideTheFinishedAlbum()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId first = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
        const AlbumId second = importAlbum(fx, QStringLiteral("Other Act"), QStringLiteral("Second"), 3, 40);
        AlbumRepo albums(fx.session->database());
        const QList<TrackId> seed = albums.tracks(first).value();
        const QList<TrackId> others = albums.tracks(second).value();
        auto genres = GenreVocabulary::load();
        QVERIFY(genres.ok());
        SystemClock clock;
        LibraryRecommendationSource source(LibraryLayout::forRoot(fx.libRoot()).dbPath, genres.value(), clock, 7);
        RecommendationRequest request;
        request.seedTracks = seed;
        request.exclude = {seed.last()};
        request.count = 3;
        const QList<TrackId> got = source.pick(request);
        QCOMPARE(got.size(), 3);
        for (TrackId t : got)
            QVERIFY(others.contains(t));  // the finished album is only used when nothing else is left
        source.refresh();                 // a library change: the pool is read again
        QVERIFY(!source.pick(request).isEmpty());  // the second album is used up: the first one is allowed now
    }

    void playStatisticsAndRecentlyPlayedAreStored()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        AlbumRepo albums(fx.session->database());
        const TrackId t = albums.tracks(album).value().first();
        LibraryPlaybackStore store(fx.session->database());
        store.recordPlay(t, 1700000000000);
        store.recordPlay(t, 1700000005000);
        QCOMPARE(fx.trackRow(t).playCount, 2);
        QCOMPARE(fx.trackRow(t).lastPlayedAt.value_or(0), 1700000005000);

        store.touchRecent(ContainerKind::Album, album.value, 1000);
        store.touchRecent(ContainerKind::Album, album.value, 2000);  // no duplicate
        RecentRepo recent(fx.session->database());
        QCOMPARE(recent.list().value().size(), 1);
    }

    void recentlyPlayedKeepsFiftyAndTheNewestFirst()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        LibraryPlaybackStore store(fx.session->database());
        // 60 playlists, registered in order
        for (int i = 1; i <= 60; ++i)
            store.touchRecent(ContainerKind::Playlist, i, i * 1000);
        RecentRepo recent(fx.session->database());
        const auto list = recent.list().value();
        QCOMPARE(list.size(), 50);
        QCOMPARE(list.first().containerId, 60);
        QCOMPARE(list.last().containerId, 11);
    }

    void theSessionSurvivesInTheDatabase()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        LibraryPlaybackStore store(fx.session->database());
        QVERIFY(store.loadSession().isEmpty());
        store.saveSession(QJsonObject{{QStringLiteral("a"), 1}, {QStringLiteral("text"), QStringLiteral("Ёлка")}});
        QCOMPARE(store.loadSession().value(QStringLiteral("text")).toString(), QStringLiteral("Ёлка"));
        fx.session.reset();
        QVERIFY(fx.open());
        LibraryPlaybackStore again(fx.session->database());
        QCOMPARE(again.loadSession().value(QStringLiteral("a")).toInt(), 1);
        again.saveSession({});
        QVERIFY(again.loadSession().isEmpty());
    }

    void playingARealAlbumEndToEndWithRestartAndDeletion()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 4, 10);
        AlbumRepo albums(fx.session->database());
        const QList<TrackId> ids = albums.tracks(album).value();

        FakeAudioBackend backend;
        StepClock clock;
        FakeSettings settings;
        backend.defaultDuration = 20000;
        LibraryTrackCatalog catalog(fx.session->database(), fx.libRoot());
        LibraryPlaybackStore store(fx.session->database());
        PlaybackService::Dependencies deps{backend, catalog, store, clock, nullptr, &settings, 1};
        auto service = std::make_unique<PlaybackService>(deps);

        PlayRequest request;
        QueueGroup g;
        g.origin = {OriginKind::Album, album.value, QStringLiteral("First")};
        for (TrackId id : ids)
            g.tracks.append({id});
        request.groups = {g};
        request.afterEnd = AfterEnd::Stop;
        QVERIFY(service->play(request));
        QCOMPARE(service->currentTrack()->title, QStringLiteral("First 1"));

        backend.advance(10000);  // 50 % of the 20 s track
        QCOMPARE(fx.trackRow(ids.at(0)).playCount, 1);
        QCOMPARE(RecentRepo(fx.session->database()).list().value().size(), 1);

        service->next();
        service->persist();

        // restart: a new service restores the session paused
        service = std::make_unique<PlaybackService>(deps);
        service->restore();
        QCOMPARE(service->currentTrack()->id, ids.at(1));
        QCOMPARE(service->state(), PlayState::Paused);

        // a library edit deletes the playing track: the service moves on
        QVERIFY(fx.session->library().deleteTrack(ids.at(1)).ok());
        service->revalidate({ids.at(1)});
        QCOMPARE(service->currentTrack()->id, ids.at(2));
        QCOMPARE(fx.verify(), QString());

        // a file deleted in Explorer: its row still exists, but the track cannot play and is skipped
        QVERIFY(QFile::remove(fx.abs(fx.trackRow(ids.at(3)).filePath)));
        QSignalSpy banner(service.get(), &PlaybackService::tracksSkipped);
        service->play();
        backend.finish();
        QCOMPARE(banner.count(), 1);
        QCOMPARE(service->state(), PlayState::Stopped);
    }

    void tagWritesLetGoOfThePlayingFile()
    {
        // The audio backend is the IFileReleaser of the tag service (SPEC 6.2): a rename while the file is
        // open must call it before the file is replaced and tell it afterwards.
        SessionFixture fx;
        QVERIFY(fx.create());
        FakeAudioBackend backend;
        SessionDependencies deps = fx.deps;
        deps.releaser = &backend;
        fx.session.reset();
        auto session = LibrarySession::open(fx.libRoot(), deps);
        QVERIFY(session.ok());
        fx.session = std::move(session).value();
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        backend.released.clear();
        backend.ready.clear();
        TrackPatch patch;
        patch.title = QStringLiteral("Other title");
        AlbumRepo albums(fx.session->database());
        const TrackId t = albums.tracks(album).value().first();
        QVERIFY(fx.session->library().updateTrack(t, patch).ok());
        QVERIFY(!backend.released.isEmpty());
        QCOMPARE(backend.released.size(), backend.ready.size());
        QCOMPARE(fx.verify(), QString());

        backend.refuseRelease = true;  // "the player cannot let go": the edit stays, the tag sync is retried later
        patch.title = QStringLiteral("Third title");
        auto r = fx.session->library().updateTrack(t, patch);
        QVERIFY(r.ok());
        QVERIFY(fx.trackRow(t).tagsDirty);
    }
};

QTEST_GUILESS_MAIN(TstLibraryPlayback)
#include "tst_library_playback.moc"
