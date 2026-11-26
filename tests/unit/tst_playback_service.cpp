#include "PlaybackFakes.h"

#include "core/playback/LibraryPlayback.h"
#include "core/playback/PlaybackService.h"

#include <QtTest>

using namespace semplice;
using namespace semplice::testing;

namespace {

QueueGroup group(OriginKind kind, qint64 id, qint64 firstTrack, int count)
{
    QueueGroup g;
    g.origin = {kind, id, QStringLiteral("G%1").arg(id)};
    for (int i = 0; i < count; ++i)
        g.tracks.append({TrackId(firstTrack + i)});
    return g;
}

PlayRequest request(const QList<QueueGroup>& groups, AfterEnd after, int startGroup = 0, int startIndex = -1)
{
    PlayRequest r;
    r.groups = groups;
    r.afterEnd = after;
    r.startGroup = startGroup;
    r.startIndex = startIndex;
    return r;
}

struct Rig {
    explicit Rig(bool withRecommender = false)
    {
        backend.durationProvider = [this](const QString& path) { return catalog.durationOfPath(path); };
        PlaybackService::Dependencies deps{backend, catalog, store, clock, withRecommender ? &recommender : nullptr, &settings, 1};
        service = std::make_unique<PlaybackService>(deps);
    }
    // Builds a fresh service on the same store / settings (a restart of the app).
    void restart(bool withRecommender = false)
    {
        service.reset();
        PlaybackService::Dependencies deps{backend, catalog, store, clock, withRecommender ? &recommender : nullptr, &settings, 1};
        service = std::make_unique<PlaybackService>(deps);
    }
    qint64 current() const { return service->currentTrack() ? service->currentTrack()->id.value : -1; }
    // Plays the current track to its natural end.
    void finishTrack() { backend.finish(); }

    FakeAudioBackend backend;
    FakeCatalog catalog;
    FakeStore store;
    StepClock clock;
    FakeRecommender recommender;
    FakeSettings settings;
    std::unique_ptr<PlaybackService> service;
};

}  // namespace

// M5: the rules of SPEC 7.2-7.5 against a fake audio backend.
class TstPlaybackService : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<semplice::PlayState>("semplice::PlayState"); }

    // ================================================================ play button rules
    void aPlaylistPlaysToItsEndAndStops()
    {
        Rig rig(true);
        rig.catalog.addRange(1, 3);
        rig.recommender.pool = {TrackId(50), TrackId(51)};
        rig.catalog.addRange(50, 2);
        QVERIFY(rig.service->play(request({group(OriginKind::Playlist, 7, 1, 3)}, AfterEnd::Stop)));
        QCOMPARE(rig.service->state(), PlayState::Playing);
        QCOMPARE(rig.current(), 1);
        QVERIFY(rig.backend.isPlaying());
        rig.finishTrack();
        QCOMPARE(rig.current(), 2);
        rig.finishTrack();
        QCOMPARE(rig.current(), 3);
        rig.finishTrack();
        QCOMPARE(rig.service->state(), PlayState::Stopped);
        QCOMPARE(rig.current(), 3);  // the last track stays shown
        QVERIFY(rig.recommender.requests.isEmpty());  // playlists never start the endless stream
        QVERIFY(!rig.service->canNext());
        rig.service->play();  // pressing play again restarts it
        QCOMPARE(rig.service->state(), PlayState::Playing);
        QCOMPARE(rig.current(), 3);
    }

    void aRenamedPlayingFileIsReloadedFromItsNewPlaceAtTheSamePosition()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        QVERIFY(rig.service->play(request({group(OriginKind::Album, 7, 1, 3)}, AfterEnd::Stop)));
        QCOMPARE(rig.current(), 1);
        // the album folder is renamed while track 1 plays: the backend let go of the old path
        rig.catalog.moveTo(1, QStringLiteral("/music/renamed/t1.mp3"));
        rig.backend.pendingRelease = ReleasedPlayback{42000, true};
        rig.backend.loads.clear();
        rig.backend.seeks.clear();
        rig.service->revalidate({TrackId(1)});
        QCOMPARE(rig.backend.loads, QStringList{QStringLiteral("/music/renamed/t1.mp3")});
        QCOMPARE(rig.backend.seeks.last(), qint64(42000));
        QCOMPARE(rig.service->state(), PlayState::Playing);
        QCOMPARE(rig.current(), 1);
    }

    void anAlbumContinuesWithRecommendationsAtTheEnd()
    {
        Rig rig(true);
        rig.catalog.addRange(1, 2);
        rig.catalog.addRange(50, 6);
        for (int i = 50; i < 56; ++i)
            rig.recommender.pool.append(TrackId(i));
        QVERIFY(rig.service->play(request({group(OriginKind::Album, 7, 1, 2)}, AfterEnd::Recommend)));
        rig.finishTrack();
        rig.finishTrack();  // album over
        QCOMPARE(rig.service->state(), PlayState::Playing);
        QCOMPARE(rig.current(), 50);
        QCOMPARE(rig.service->currentOrigin()->kind, OriginKind::Recommendations);
        // seed = the album that just finished
        QCOMPARE(rig.recommender.requests.first().seedTracks, (QList<TrackId>{TrackId(1), TrackId(2)}));
        // three tracks are ready ahead ("Up next" is accurate)
        QCOMPARE(rig.service->queue().upNext(10).size(), 3);
        rig.finishTrack();
        QCOMPARE(rig.current(), 51);
        QCOMPARE(rig.service->queue().upNext(10).size(), 3);  // topped up again
        QVERIFY(!rig.service->queue().upNext(10).contains(QueueTrack{TrackId(50)}));
    }

    void recommendationsStopWhenThePoolIsEmpty()
    {
        Rig rig(true);
        rig.catalog.addRange(1, 1);
        QVERIFY(rig.service->play(request({group(OriginKind::Album, 7, 1, 1)}, AfterEnd::Recommend)));
        rig.finishTrack();
        QCOMPARE(rig.service->state(), PlayState::Stopped);
    }

    void recommendationsCountAsPlaysButNeverTouchRecentlyPlayed()
    {
        Rig rig(true);
        rig.catalog.addRange(1, 1, 60000);
        rig.catalog.addRange(50, 3, 60000);
        rig.recommender.pool = {TrackId(50), TrackId(51), TrackId(52)};
        rig.service->play(request({group(OriginKind::Album, 7, 1, 1)}, AfterEnd::Recommend));
        rig.backend.advance(30000);  // 50 % of 60 s
        QCOMPARE(rig.store.recents.size(), 1);
        rig.backend.advance(30000);  // ends -> recommendation 50 starts
        QCOMPARE(rig.current(), 50);
        rig.backend.advance(30000);
        QCOMPARE(rig.store.plays.value(50), 1);
        QCOMPARE(rig.store.recents.size(), 1);  // still only the album
    }

    void aSingleTrackFromSearchIsAQueueOfOne()
    {
        Rig rig(true);
        rig.catalog.addRange(1, 5);
        rig.recommender.pool = {TrackId(2)};
        QVERIFY(rig.service->play(request({group(OriginKind::Album, 0, 3, 1)}, AfterEnd::Stop)));
        rig.finishTrack();
        QCOMPARE(rig.service->state(), PlayState::Stopped);
        QVERIFY(rig.recommender.requests.isEmpty());
    }

    // ================================================================ carousel groups
    void carouselItemsAreIndependentGroupsAndThePlayerCanGoBack()
    {
        Rig rig(true);
        rig.catalog.addRange(10, 2);
        rig.catalog.addRange(20, 2);
        rig.catalog.addRange(30, 2);
        rig.catalog.addRange(90, 3);
        rig.recommender.pool = {TrackId(90), TrackId(91), TrackId(92)};
        rig.service->play(request({group(OriginKind::Carousel, 1, 10, 2), group(OriginKind::Carousel, 2, 20, 2), group(OriginKind::Carousel, 3, 30, 2)},
                                  AfterEnd::Recommend, 1));
        QCOMPARE(rig.current(), 20);
        QVERIFY(rig.service->canPrevious());
        rig.service->previous();  // into the earlier group
        QCOMPARE(rig.current(), 11);
        rig.finishTrack();
        QCOMPARE(rig.current(), 20);
        rig.finishTrack();
        rig.finishTrack();
        QCOMPARE(rig.current(), 30);
        rig.finishTrack();
        rig.finishTrack();  // after the last group the recommendations start
        QCOMPARE(rig.current(), 90);
    }

    // ================================================================ previous / next
    void previousRestartsAfterFiveSecondsAndGoesBackBefore()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 3)}, AfterEnd::Stop));
        QVERIFY(!rig.service->canPrevious());  // very start of playback
        rig.service->previous();
        QCOMPARE(rig.current(), 1);
        rig.backend.advance(3000);
        QVERIFY(!rig.service->canPrevious());
        rig.service->next();
        QCOMPARE(rig.current(), 2);
        QVERIFY(rig.service->canPrevious());
        rig.backend.advance(4000);  // <= 5 s: previous track
        rig.service->previous();
        QCOMPARE(rig.current(), 1);
        rig.service->next();
        rig.backend.advance(6000);  // > 5 s: restart
        QVERIFY(rig.service->canPrevious());
        const int loads = rig.backend.loads.size();
        rig.service->previous();
        QCOMPARE(rig.current(), 2);
        QCOMPARE(rig.backend.loads.size(), loads);  // not reloaded, just rewound
        QCOMPARE(rig.backend.seeks.last(), 0);
    }

    void nextOnTheLastTrackMovesOnOrIsDisabled()
    {
        Rig rig(true);
        rig.catalog.addRange(1, 2);
        rig.catalog.addRange(10, 2);
        rig.catalog.addRange(90, 2);
        rig.recommender.pool = {TrackId(90), TrackId(91)};
        rig.service->play(request({group(OriginKind::Carousel, 1, 1, 2), group(OriginKind::Carousel, 2, 10, 2)}, AfterEnd::Recommend));
        rig.service->next();
        QCOMPARE(rig.current(), 2);
        rig.service->next();  // last track of group 1 -> group 2
        QCOMPARE(rig.current(), 10);
        rig.service->next();
        rig.service->next();  // last of everything -> recommendations
        QCOMPARE(rig.current(), 90);

        Rig stop;
        stop.catalog.addRange(1, 2);
        stop.service->play(request({group(OriginKind::Playlist, 1, 1, 2)}, AfterEnd::Stop));
        stop.service->next();
        QVERIFY(!stop.service->canNext());
        stop.service->next();  // nothing happens
        QCOMPARE(stop.current(), 2);
        QCOMPARE(stop.service->state(), PlayState::Playing);
    }

    // ================================================================ repeat + shuffle
    void repeatOneLoopsTheTrackButNextStillMovesOn()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 3)}, AfterEnd::Stop));
        rig.service->setRepeat(RepeatMode::One);
        rig.finishTrack();
        QCOMPARE(rig.current(), 1);
        QCOMPARE(rig.service->state(), PlayState::Playing);
        rig.finishTrack();
        QCOMPARE(rig.current(), 1);
        rig.service->next();
        QCOMPARE(rig.current(), 2);
    }

    void repeatAllRepeatsTheCurrentGroupOnly()
    {
        Rig rig;
        rig.catalog.addRange(1, 2);
        rig.catalog.addRange(10, 2);
        rig.service->play(request({group(OriginKind::Carousel, 1, 1, 2), group(OriginKind::Carousel, 2, 10, 2)}, AfterEnd::Stop));
        rig.service->setRepeat(RepeatMode::All);
        rig.finishTrack();
        rig.finishTrack();  // wraps inside group 1
        QCOMPARE(rig.current(), 1);
        QCOMPARE(rig.service->queue().currentGroupIndex(), 0);
        QVERIFY(rig.service->canNext());
    }

    void shuffleKeepsTheCurrentTrackPlayingAndCoversTheRest()
    {
        Rig rig;
        rig.catalog.addRange(1, 8);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 8)}, AfterEnd::Stop, 0, 2));
        QCOMPARE(rig.current(), 3);
        const int loads = rig.backend.loads.size();
        rig.service->setShuffle(true);
        QCOMPARE(rig.current(), 3);
        QCOMPARE(rig.backend.loads.size(), loads);  // switching shuffle does not restart the song
        QSet<qint64> seen{3};
        for (int i = 0; i < 5; ++i) {
            rig.finishTrack();
            seen.insert(rig.current());
        }
        QCOMPARE(seen.size(), 6);  // 3 + five others; tracks 1 and 2 were never played and may follow
        rig.service->setShuffle(false);
        QVERIFY(!rig.service->shuffle());
    }

    void shufflePreviousWalksThePlayedHistory()
    {
        Rig rig;
        rig.catalog.addRange(1, 6);
        rig.service->setShuffle(true);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 6)}, AfterEnd::Stop));
        QList<qint64> played{rig.current()};
        for (int i = 0; i < 3; ++i) {
            rig.service->next();
            played.append(rig.current());
        }
        for (int i = 2; i >= 0; --i) {
            rig.service->previous();
            QCOMPARE(rig.current(), played.at(i));
        }
    }

    void clickingTheQueueJumpsToATrack()
    {
        Rig rig;
        rig.catalog.addRange(1, 5);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 5)}, AfterEnd::Stop));
        QVERIFY(rig.service->playAt(0, 3));
        QCOMPARE(rig.current(), 4);
        rig.service->previous();
        QCOMPARE(rig.current(), 1);
        QVERIFY(!rig.service->playAt(4, 0));
    }

    // ================================================================ pause / transport
    void pausePlayAndSeek()
    {
        Rig rig;
        rig.catalog.addRange(1, 2);
        QSignalSpy states(rig.service.get(), &PlaybackService::stateChanged);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 2)}, AfterEnd::Stop));
        rig.service->pause();
        QCOMPARE(rig.service->state(), PlayState::Paused);
        QVERIFY(!rig.backend.isPlaying());
        rig.service->toggle();
        QCOMPARE(rig.service->state(), PlayState::Playing);
        rig.service->seek(42000);
        QCOMPARE(rig.backend.positionMs(), 42000);
        rig.service->stop();
        QCOMPARE(rig.service->state(), PlayState::Stopped);
        QCOMPARE(states.count(), 4);  // playing, paused, playing, stopped
    }

    void skippingWhilePausedStartsPlaying()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 3)}, AfterEnd::Stop));
        rig.service->pause();
        rig.service->next();
        QCOMPARE(rig.current(), 2);
        QCOMPARE(rig.service->state(), PlayState::Playing);
    }

    void theNextTrackIsPreloadedForGaplessPlayback()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 3)}, AfterEnd::Stop));
        QCOMPARE(rig.backend.preloaded.last(), FakeCatalog::pathOf(2));
        rig.finishTrack();
        QCOMPARE(rig.backend.preloaded.last(), FakeCatalog::pathOf(3));
    }

    // ================================================================ unplayable files
    void unplayableTracksAreSkippedWithOneBanner()
    {
        Rig rig;
        rig.catalog.addRange(1, 6);
        rig.catalog.remove(2);   // file gone
        rig.catalog.remove(3);   // file gone
        rig.backend.failOnLoad.insert(FakeCatalog::pathOf(4));  // present but not decodable
        QSignalSpy banner(rig.service.get(), &PlaybackService::tracksSkipped);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 6)}, AfterEnd::Stop));
        QCOMPARE(rig.current(), 1);
        QCOMPARE(banner.count(), 0);
        rig.finishTrack();
        QCOMPARE(rig.current(), 5);
        QCOMPARE(banner.count(), 1);
        QCOMPARE(banner.first().first().toInt(), 3);
        // the skipped tracks are gone from the queue
        QCOMPARE(rig.service->queue().group(0).tracks.size(), 3);
    }

    void aFirstTrackThatCannotPlayStartsTheNextOne()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        rig.catalog.remove(1);
        QSignalSpy banner(rig.service.get(), &PlaybackService::tracksSkipped);
        QVERIFY(rig.service->play(request({group(OriginKind::Album, 1, 1, 3)}, AfterEnd::Stop)));
        QCOMPARE(rig.current(), 2);
        QCOMPARE(banner.count(), 1);
    }

    void aQueueOfOnlyBrokenTracksStopsWithOneBanner()
    {
        Rig rig;
        QSignalSpy banner(rig.service.get(), &PlaybackService::tracksSkipped);
        QVERIFY(!rig.service->play(request({group(OriginKind::Album, 1, 1, 3)}, AfterEnd::Stop)));
        QCOMPARE(rig.service->state(), PlayState::Stopped);
        QCOMPARE(banner.count(), 1);
        QCOMPARE(banner.first().first().toInt(), 3);
    }

    void aDecodeErrorDuringPlaybackSkipsToTheNextTrack()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        QSignalSpy banner(rig.service.get(), &PlaybackService::tracksSkipped);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 3)}, AfterEnd::Stop));
        rig.backend.failLater(FakeCatalog::pathOf(1));
        QCOMPARE(rig.current(), 2);
        QCOMPARE(rig.service->state(), PlayState::Playing);
        QCOMPARE(banner.count(), 1);
    }

    // ================================================================ deletions
    void deletingTheCurrentTrackContinuesWithTheNext()
    {
        Rig rig;
        rig.catalog.addRange(1, 4);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 4)}, AfterEnd::Stop, 0, 1));
        QCOMPARE(rig.current(), 2);
        rig.catalog.remove(2);
        rig.service->revalidate({TrackId(2)});
        QCOMPARE(rig.current(), 3);
        QCOMPARE(rig.service->state(), PlayState::Playing);
        QCOMPARE(rig.service->queue().group(0).tracks.size(), 3);
    }

    void deletingTheNextTrackUpdatesThePreload()
    {
        Rig rig;
        rig.catalog.addRange(1, 4);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 4)}, AfterEnd::Stop));
        rig.catalog.remove(2);
        rig.service->revalidate({TrackId(2)});
        QCOMPARE(rig.current(), 1);
        QCOMPARE(rig.backend.preloaded.last(), FakeCatalog::pathOf(3));
        rig.finishTrack();
        QCOMPARE(rig.current(), 3);
    }

    void deletingTheLastTrackThatIsPlayingStopsTheMusic()
    {
        Rig rig;
        rig.catalog.addRange(1, 1);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 1)}, AfterEnd::Stop));
        rig.catalog.remove(1);
        rig.service->revalidate({TrackId(1)});
        QCOMPARE(rig.service->state(), PlayState::Stopped);
        QVERIFY(!rig.service->currentTrack());
    }

    void deletingWhilePausedKeepsItPaused()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        rig.service->play(request({group(OriginKind::Album, 1, 1, 3)}, AfterEnd::Stop));
        rig.service->pause();
        rig.catalog.remove(1);
        rig.service->revalidate({TrackId(1)});
        QCOMPARE(rig.current(), 2);
        QCOMPARE(rig.service->state(), PlayState::Paused);
    }

    // ================================================================ statistics (SPEC 7.4, 7.5)
    void aTrackCountsAfter30SecondsOrHalfIfShorter_data()
    {
        QTest::addColumn<qint64>("durationMs");
        QTest::addColumn<qint64>("needMs");
        QTest::newRow("long track: 30 s") << qint64(200000) << qint64(30000);
        QTest::newRow("exactly 60 s: 30 s") << qint64(60000) << qint64(30000);
        QTest::newRow("40 s: 20 s") << qint64(40000) << qint64(20000);
        QTest::newRow("10 s: 5 s") << qint64(10000) << qint64(5000);
    }
    void aTrackCountsAfter30SecondsOrHalfIfShorter()
    {
        QFETCH(qint64, durationMs);
        QFETCH(qint64, needMs);
        Rig rig;
        rig.catalog.add(1, durationMs);
        rig.catalog.add(2, durationMs);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 2)}, AfterEnd::Stop));
        rig.backend.advance(needMs - 1000);
        QCOMPARE(rig.store.plays.value(1), 0);
        QVERIFY(rig.store.recents.isEmpty());
        rig.backend.advance(1000);
        QCOMPARE(rig.store.plays.value(1), 1);
        QCOMPARE(rig.store.recents.size(), 1);
        QCOMPARE(rig.store.recents.first().second, 5);
        rig.backend.advance(durationMs);  // to the end: no second count for the same listen
        QCOMPARE(rig.store.plays.value(1), 1);
    }

    void seekingAndPausingDoNotCountAsListening()
    {
        Rig rig;
        rig.catalog.add(1, 200000);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 1)}, AfterEnd::Stop));
        rig.backend.advance(20000);
        rig.service->seek(120000);  // jump forward
        rig.backend.advance(5000);
        QCOMPARE(rig.store.plays.value(1), 0);  // 25 s listened, not 125 s
        rig.service->pause();
        rig.service->play();
        rig.backend.advance(4000);
        QCOMPARE(rig.store.plays.value(1), 0);
        rig.backend.advance(2000);
        QCOMPARE(rig.store.plays.value(1), 1);
    }

    void aBigPositionJumpFromTheBackendIsNotListeningTime()
    {
        Rig rig;
        rig.catalog.add(1, 200000);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 1)}, AfterEnd::Stop));
        rig.backend.advance(10000, 10000);  // one update 10 s later (e.g. the window was frozen)
        QCOMPARE(rig.store.plays.value(1), 0);
    }

    void repeatingATrackCountsEveryListen()
    {
        Rig rig;
        rig.catalog.add(1, 40000);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 1)}, AfterEnd::Stop));
        rig.service->setRepeat(RepeatMode::One);
        rig.backend.advance(40000);
        rig.backend.advance(40000);
        QCOMPARE(rig.store.plays.value(1), 2);
        QCOMPARE(rig.store.recents.size(), 1);  // the album is registered once
    }

    void recentlyPlayedMovesTheContainerToTheFrontWithoutDuplicates()
    {
        Rig rig;
        rig.catalog.addRange(1, 2, 20000);
        rig.catalog.addRange(10, 2, 20000);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 2)}, AfterEnd::Stop));
        rig.backend.advance(10000);
        rig.service->play(request({group(OriginKind::Playlist, 9, 10, 2)}, AfterEnd::Stop));
        rig.backend.advance(10000);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 2)}, AfterEnd::Stop));
        rig.backend.advance(10000);
        QCOMPARE(rig.store.recents.size(), 2);
        QCOMPARE(rig.store.recents.at(0), qMakePair(static_cast<int>(ContainerKind::Album), qint64(5)));
        QCOMPARE(rig.store.recents.at(1), qMakePair(static_cast<int>(ContainerKind::Playlist), qint64(9)));
    }

    void anAlbumIsRegisteredOnceEvenWhenSeveralOfItsTracksPlay()
    {
        Rig rig;
        rig.catalog.addRange(1, 3, 20000);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 3)}, AfterEnd::Stop));
        rig.backend.advance(60000);  // all three tracks
        QCOMPARE(rig.store.touches.size(), 1);
        QCOMPARE(rig.store.plays.size(), 3);
    }

    void aFirstTrackThatWasSkippedDoesNotBlockRegistration()
    {
        Rig rig;
        rig.catalog.addRange(1, 3, 20000);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 3)}, AfterEnd::Stop, 0, 2));
        rig.backend.advance(10000);
        QCOMPARE(rig.store.touches.size(), 1);
    }

    void discographyRegistersEachAlbumSeparately()
    {
        Rig rig;
        rig.catalog.addRange(1, 4, 20000);
        QueueGroup g;
        g.origin = {OriginKind::Discography, 3, QStringLiteral("Artist")};
        g.tracks = {{TrackId(1), 11, ContainerKind::Album}, {TrackId(2), 11, ContainerKind::Album},
                    {TrackId(3), 12, ContainerKind::Album}, {TrackId(4), 12, ContainerKind::Album}};
        rig.service->play(request({g}, AfterEnd::Recommend));
        rig.backend.advance(80000);
        QCOMPARE(rig.store.touches.size(), 2);
        QCOMPARE(rig.store.recents.at(0).second, 12);
        QCOMPARE(rig.store.recents.at(1).second, 11);
    }

    void aCarouselGroupWithMixedSourcesRegistersAlbumsAndPlaylists()
    {
        Rig rig;
        rig.catalog.addRange(1, 2, 20000);
        QueueGroup g;
        g.origin = {OriginKind::Carousel, 1, QStringLiteral("Mix")};
        g.tracks = {{TrackId(1), 11, ContainerKind::Album}, {TrackId(2), 21, ContainerKind::Playlist}};
        rig.service->play(request({g}, AfterEnd::Recommend));
        rig.backend.advance(40000);
        QCOMPARE(rig.store.recents.at(0), qMakePair(static_cast<int>(ContainerKind::Playlist), qint64(21)));
        QCOMPARE(rig.store.recents.at(1), qMakePair(static_cast<int>(ContainerKind::Album), qint64(11)));
    }

    // ================================================================ persistence (SPEC 7.3)
    void theSessionComesBackPausedAtTheSamePlace()
    {
        Rig rig;
        rig.catalog.addRange(1, 4);
        rig.service->setRepeat(RepeatMode::All);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 4)}, AfterEnd::Recommend, 0, 1));
        rig.service->next();
        rig.backend.advance(12000);
        rig.service->persist();

        rig.restart();
        rig.backend.loads.clear();
        rig.service->restore();
        QCOMPARE(rig.current(), 3);
        QCOMPARE(rig.service->state(), PlayState::Paused);
        QVERIFY(!rig.backend.isPlaying());
        QCOMPARE(rig.backend.loads, QStringList{FakeCatalog::pathOf(3)});
        QCOMPARE(rig.backend.seeks.last(), 12000);
        QCOMPARE(rig.service->repeat(), RepeatMode::All);
        QCOMPARE(rig.service->queue().afterEnd(), AfterEnd::Recommend);
        QCOMPARE(rig.service->currentOrigin()->title, QStringLiteral("G5"));
        // and it plays on from there
        rig.service->play();
        QVERIFY(rig.backend.isPlaying());
    }

    void shuffleOrderIsRestoredToo()
    {
        Rig rig;
        rig.catalog.addRange(1, 10);
        rig.service->setShuffle(true);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 10)}, AfterEnd::Stop));
        rig.service->next();
        rig.service->persist();
        const QList<QueueTrack> before = rig.service->queue().currentGroupInOrder();
        rig.restart();
        rig.service->restore();
        QVERIFY(rig.service->shuffle());
        QCOMPARE(rig.service->queue().currentGroupInOrder(), before);
    }

    void missingTracksAreDroppedOnRestoreWithOneBanner()
    {
        Rig rig;
        rig.catalog.addRange(1, 5);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 5)}, AfterEnd::Stop, 0, 1));
        rig.service->persist();
        rig.catalog.remove(1);
        rig.catalog.remove(4);
        rig.restart();
        QSignalSpy banner(rig.service.get(), &PlaybackService::tracksSkipped);
        rig.service->restore();
        QCOMPARE(rig.current(), 2);
        QCOMPARE(banner.count(), 1);
        QCOMPARE(banner.first().first().toInt(), 2);
        QCOMPARE(rig.service->queue().group(0).tracks.size(), 3);
        QCOMPARE(rig.service->state(), PlayState::Paused);
    }

    void ifTheCurrentTrackIsGoneTheNextOneIsRestoredFromTheStart()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 3)}, AfterEnd::Stop, 0, 1));
        rig.backend.advance(9000);
        rig.service->persist();
        rig.catalog.remove(2);
        rig.restart();
        rig.service->restore();
        QCOMPARE(rig.current(), 3);
        QCOMPARE(rig.backend.positionMs(), 0);
    }

    void aSessionWhereNothingSurvivesRestoresNothing()
    {
        Rig rig;
        rig.catalog.addRange(1, 2);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 2)}, AfterEnd::Stop));
        rig.service->persist();
        rig.catalog.remove(1);
        rig.catalog.remove(2);
        rig.restart();
        QSignalSpy banner(rig.service.get(), &PlaybackService::tracksSkipped);
        rig.service->restore();
        QVERIFY(!rig.service->currentTrack());
        QCOMPARE(rig.service->state(), PlayState::Stopped);
        QCOMPARE(banner.count(), 1);
    }

    void noSessionOrABrokenOneChangesNothing()
    {
        Rig rig;
        rig.service->restore();
        QVERIFY(!rig.service->currentTrack());
        rig.store.session = QJsonObject{{QStringLiteral("queue"), QJsonObject{{QStringLiteral("groups"), 5}}}};
        rig.restart();
        rig.service->restore();
        QVERIFY(!rig.service->currentTrack());
    }

    void theSessionIsSavedOnTrackChangesAndStopped()
    {
        Rig rig;
        rig.catalog.addRange(1, 2);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 2)}, AfterEnd::Stop));
        QVERIFY(!rig.store.session.isEmpty());
        rig.service->next();
        rig.service->next();  // no-op at the end
        QVERIFY(!rig.store.session.isEmpty());
    }

    // ================================================================ volume
    void volumeAndMuteAreStoredSeparatelyAndRestored()
    {
        Rig rig;
        QCOMPARE(rig.backend.volume, 0.5);
        rig.service->setVolume(0.8);
        rig.service->setMuted(true);
        QCOMPARE(rig.settings.m_volume, 0.8);
        QVERIFY(rig.settings.m_muted);
        QCOMPARE(rig.backend.volume, 0.8);  // the level is kept while muted
        QVERIFY(rig.backend.muted);
        rig.service->setVolume(7);
        QCOMPARE(rig.service->volume(), 1.0);
        rig.restart();
        QCOMPARE(rig.service->volume(), 1.0);
        QVERIFY(rig.service->muted());
        QVERIFY(rig.backend.muted);
    }

    // ================================================================ system integration glue
    void windowsIsNotToldTwiceAboutTheSameTrackAndState()
    {
        // a save announces the current track again: the media panel and the taskbar buttons (calls into other processes) stay quiet
        Rig rig;
        rig.catalog.addRange(1, 3);
        FakeControls controls;
        FakeThumbBar thumbs;
        SystemIntegration glue(*rig.service, &controls, &thumbs);
        rig.service->play(request({group(OriginKind::Album, 5, 1, 3)}, AfterEnd::Stop));
        const int metadataBefore = controls.metadataCalls, stateBefore = controls.stateCalls, thumbsBefore = thumbs.calls;
        QVERIFY(metadataBefore > 0);
        emit rig.service->currentChanged();
        emit rig.service->queueChanged();
        QCOMPARE(controls.metadataCalls, metadataBefore);
        QCOMPARE(controls.stateCalls, stateBefore);
        QCOMPARE(thumbs.calls, thumbsBefore);
        rig.service->pause();  // a real change still goes through
        QVERIFY(controls.stateCalls > stateBefore);
        QVERIFY(thumbs.calls > thumbsBefore);
    }

    // ================================================================ system integration glue
    void systemControlsFollowThePlayerAndDriveIt()
    {
        Rig rig;
        rig.catalog.addRange(1, 3);
        FakeControls controls;
        FakeThumbBar thumbs;
        SystemIntegration glue(*rig.service, &controls, &thumbs);
        QVERIFY(!controls.canPlay);
        QVERIFY(!thumbs.canPlay);

        rig.service->play(request({group(OriginKind::Album, 5, 1, 3)}, AfterEnd::Stop));
        QCOMPARE(controls.metadata.title, QStringLiteral("Track 1"));
        QCOMPARE(controls.metadata.album, QStringLiteral("Album"));
        QCOMPARE(controls.state, PlayState::Playing);
        QVERIFY(controls.canNext);
        QVERIFY(!controls.canPrev);
        QVERIFY(thumbs.isPlaying);

        emit controls.nextRequested();
        QCOMPARE(rig.current(), 2);
        QVERIFY(controls.canPrev);
        emit thumbs.playPauseClicked();
        QCOMPARE(rig.service->state(), PlayState::Paused);
        QVERIFY(!thumbs.isPlaying);
        QCOMPARE(controls.state, PlayState::Paused);
        emit controls.playRequested();
        QCOMPARE(rig.service->state(), PlayState::Playing);
        emit controls.pauseRequested();
        QCOMPARE(rig.service->state(), PlayState::Paused);
        emit controls.toggleRequested();
        emit thumbs.nextClicked();
        QCOMPARE(rig.current(), 3);
        QVERIFY(!thumbs.canNext);  // the last track of a playlist-like queue
        emit thumbs.previousClicked();
        QCOMPARE(rig.current(), 2);
        rig.service->stop();
        QCOMPARE(controls.state, PlayState::Stopped);
    }
};

QTEST_GUILESS_MAIN(TstPlaybackService)
#include "tst_playback_service.moc"
