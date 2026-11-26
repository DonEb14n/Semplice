#include "core/playback/PlayQueue.h"

#include <QtTest>

using namespace semplice;

namespace {

QueueGroup makeGroup(OriginKind kind, qint64 id, int firstTrackId, int count)
{
    QueueGroup g;
    g.origin = {kind, id, QStringLiteral("G%1").arg(id)};
    for (int i = 0; i < count; ++i)
        g.tracks.append({TrackId(firstTrackId + i), 0});
    return g;
}

PlayRequest requestOf(const QList<QueueGroup>& groups, int startGroup = 0, int startIndex = -1, AfterEnd after = AfterEnd::Stop)
{
    PlayRequest r;
    r.groups = groups;
    r.startGroup = startGroup;
    r.startIndex = startIndex;
    r.afterEnd = after;
    return r;
}

qint64 cur(const PlayQueue& q) { return q.currentTrack() ? q.currentTrack()->track.value : -1; }

// Plays the queue to its end with natural advances and returns the visited track ids.
QList<qint64> playThrough(PlayQueue& q, int limit = 1000)
{
    QList<qint64> seen;
    seen.append(cur(q));
    for (int i = 0; i < limit; ++i) {
        if (q.advance(true) != PlayQueue::Step::Moved)
            break;
        seen.append(cur(q));
    }
    return seen;
}

}  // namespace

class TstPlayQueue : public QObject {
    Q_OBJECT

private slots:
    void emptyRequestsAreRefused()
    {
        PlayQueue q;
        QVERIFY(!q.start(requestOf({}), false, RepeatMode::Off));
        QVERIFY(!q.start(requestOf({makeGroup(OriginKind::Album, 1, 1, 0)}), false, RepeatMode::Off));
        QVERIFY(q.isEmpty());
        QVERIFY(!q.currentTrack());
    }

    void playsAGroupInOrderAndStopsAtTheEnd()
    {
        PlayQueue q;
        QVERIFY(q.start(requestOf({makeGroup(OriginKind::Playlist, 1, 10, 3)}), false, RepeatMode::Off));
        QCOMPARE(playThrough(q), (QList<qint64>{10, 11, 12}));
        QCOMPARE(q.advance(true), PlayQueue::Step::End);
        QCOMPARE(cur(q), 12);  // the pointer stays on the last track
    }

    void afterTheEndRecommendationsAreRequestedOnlyWhenAsked()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 1)}, 0, -1, AfterEnd::Recommend), false, RepeatMode::Off);
        QCOMPARE(q.advance(true), PlayQueue::Step::NeedRecommendations);
        q.appendGroup(makeGroup(OriginKind::Recommendations, 0, 100, 2));
        QCOMPARE(q.advance(true), PlayQueue::Step::Moved);
        QCOMPARE(cur(q), 100);
        QCOMPARE(q.currentOrigin().kind, OriginKind::Recommendations);
    }

    void startsAtTheRequestedTrackAndGroup()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 3), makeGroup(OriginKind::Album, 2, 20, 3)}, 1, 1), false, RepeatMode::Off);
        QCOMPARE(cur(q), 21);
        QCOMPARE(q.currentGroupIndex(), 1);
    }

    void carouselGroupsFollowEachOtherAndThePlayerCanGoBack()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Carousel, 1, 10, 2), makeGroup(OriginKind::Carousel, 2, 20, 2),
                           makeGroup(OriginKind::Carousel, 3, 30, 2)}, 1, -1, AfterEnd::Recommend),
                false, RepeatMode::Off);
        QCOMPARE(cur(q), 20);
        // nothing was played before, but the earlier group exists: Previous goes into it
        QVERIFY(q.canPrevious());
        QVERIFY(q.goPrevious());
        QCOMPARE(q.currentGroupIndex(), 0);
        QCOMPARE(cur(q), 11);  // its last track
        // forward again through everything
        QCOMPARE(playThrough(q), (QList<qint64>{11, 20, 21, 30, 31}));
        QCOMPARE(q.advance(true), PlayQueue::Step::NeedRecommendations);
    }

    void previousWalksTheRealPlayedHistory()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 5)}), false, RepeatMode::Off);
        QVERIFY(!q.canPrevious());
        QVERIFY(!q.goPrevious());
        q.advance(false);  // 11
        q.jumpTo(0, 4);    // 14 (the user clicked in the queue)
        QCOMPARE(cur(q), 14);
        QVERIFY(q.goPrevious());
        QCOMPARE(cur(q), 11);  // history, not "13"
        QVERIFY(q.goPrevious());
        QCOMPARE(cur(q), 10);
        QVERIFY(!q.canPrevious());
    }

    void repeatOneRepeatsOnlyWhenTheTrackEndsByItself()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 3)}), false, RepeatMode::One);
        QCOMPARE(q.advance(true), PlayQueue::Step::Moved);
        QCOMPARE(cur(q), 10);
        QVERIFY(!q.peekNext());
        QCOMPARE(q.advance(false), PlayQueue::Step::Moved);  // the Next button still moves on
        QCOMPARE(cur(q), 11);
    }

    void repeatAllRepeatsTheCurrentGroupOnly()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Carousel, 1, 10, 2), makeGroup(OriginKind::Carousel, 2, 20, 2)}), false, RepeatMode::All);
        QCOMPARE(q.advance(true), PlayQueue::Step::Moved);
        QCOMPARE(q.advance(true), PlayQueue::Step::Moved);  // wraps inside group 1
        QCOMPARE(cur(q), 10);
        QCOMPARE(q.currentGroupIndex(), 0);
        q.advance(true);
        QCOMPARE(q.advance(false), PlayQueue::Step::Moved);  // Next on the last track wraps too
        QCOMPARE(cur(q), 10);
    }

    void repeatAllNeverMakesRecommendationsEndless()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Recommendations, 0, 10, 2)}), false, RepeatMode::All);
        q.advance(true);
        QCOMPARE(q.advance(true), PlayQueue::Step::End);
    }

    // ---- shuffle
    void shuffleOnKeepsTheCurrentTrackFirstAndPlaysEveryTrackOnce()
    {
        PlayQueue q(7);
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 8)}, 0, 3), false, RepeatMode::Off);
        QCOMPARE(cur(q), 13);
        q.advance(false);  // 14: now 13 and 14 were played
        q.setShuffle(true);
        QCOMPARE(cur(q), 14);
        const QList<qint64> rest = playThrough(q);
        QCOMPARE(rest.first(), 14);
        QCOMPARE(rest.size(), 7);  // 14 + the six that were not played yet; 13 is not repeated
        QVERIFY(!rest.contains(13));
        QSet<qint64> unique(rest.begin(), rest.end());
        QCOMPARE(unique.size(), rest.size());
    }

    void shuffleOffRestoresTheNaturalOrderFromTheCurrentTrack()
    {
        PlayQueue q(3);
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 6)}), true, RepeatMode::Off);
        q.advance(false);
        q.advance(false);
        const qint64 now = cur(q);
        q.setShuffle(false);
        QCOMPARE(cur(q), now);
        const QList<qint64> rest = playThrough(q);
        QList<qint64> expected;
        for (qint64 t = now; t <= 15; ++t)
            expected.append(t);
        QCOMPARE(rest, expected);
    }

    void shuffledPassesDoNotRepeatWithinAPassAndRepeatAllStartsANewPass()
    {
        PlayQueue q(11);
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 5)}), true, RepeatMode::All);
        QSet<qint64> pass1;
        pass1.insert(cur(q));
        for (int i = 0; i < 4; ++i) {
            q.advance(true);
            pass1.insert(cur(q));
        }
        QCOMPARE(pass1.size(), 5);
        const qint64 lastOfPass = cur(q);
        QCOMPARE(q.advance(true), PlayQueue::Step::Moved);  // new pass
        QVERIFY(cur(q) != lastOfPass);
        QSet<qint64> pass2;
        pass2.insert(cur(q));
        for (int i = 0; i < 4; ++i) {
            q.advance(true);
            pass2.insert(cur(q));
        }
        QCOMPARE(pass2.size(), 5);
    }

    void shuffleWithoutAStartIndexStartsAnywhereButCoversEverything()
    {
        for (quint64 seed = 1; seed < 20; ++seed) {
            PlayQueue q(seed);
            q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 6)}), true, RepeatMode::Off);
            const QList<qint64> all = playThrough(q);
            QCOMPARE(all.size(), 6);
            QCOMPARE(QSet<qint64>(all.begin(), all.end()).size(), 6);
        }
    }

    void shuffleDoesNotCrossGroups()
    {
        PlayQueue q(5);
        q.start(requestOf({makeGroup(OriginKind::Carousel, 1, 10, 4), makeGroup(OriginKind::Carousel, 2, 20, 4)}), true, RepeatMode::Off);
        const QList<qint64> all = playThrough(q);
        QCOMPARE(all.size(), 8);
        for (int i = 0; i < 4; ++i)
            QVERIFY(all.at(i) < 20);
        for (int i = 4; i < 8; ++i)
            QVERIFY(all.at(i) >= 20);
    }

    void previousUnderShuffleWalksThePlayedHistory()
    {
        PlayQueue q(9);
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 6)}), true, RepeatMode::Off);
        QList<qint64> played{cur(q)};
        for (int i = 0; i < 3; ++i) {
            q.advance(false);
            played.append(cur(q));
        }
        for (int i = 2; i >= 0; --i) {
            QVERIFY(q.goPrevious());
            QCOMPARE(cur(q), played.at(i));
        }
        QVERIFY(!q.canPrevious());
        // and forward again the same way
        q.advance(false);
        QCOMPARE(cur(q), played.at(1));
    }

    // ---- views
    void upNextShowsTheFollowingTracksThenTheFollowingGroups()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Carousel, 1, 10, 3), makeGroup(OriginKind::Carousel, 2, 20, 2)}), false, RepeatMode::Off);
        q.advance(false);
        const QList<QueueTrack> next = q.upNext(3);
        QCOMPARE(next.size(), 3);
        QCOMPARE(next.at(0).track.value, 12);
        QCOMPARE(next.at(1).track.value, 20);
        QCOMPARE(q.playedBefore(5).size(), 1);
        QCOMPARE(q.playedBefore(5).first().track.value, 10);
        QCOMPARE(q.currentGroupInOrder().size(), 3);
        QCOMPARE(q.peekNext()->track.value, 12);
    }

    // ---- removal
    void removingUpcomingTracksKeepsThePointer()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 5)}, 0, 1), false, RepeatMode::Off);
        const auto r = q.removeTracks({TrackId(12), TrackId(10)});
        QCOMPARE(r.removed, 2);
        QVERIFY(!r.currentRemoved);
        QCOMPARE(cur(q), 11);
        QCOMPARE(playThrough(q), (QList<qint64>{11, 13, 14}));
    }

    void removingTheCurrentTrackContinuesWithTheNextOne()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 4)}, 0, 1), false, RepeatMode::Off);
        const auto r = q.removeTracks({TrackId(11)});
        QVERIFY(r.currentRemoved);
        QVERIFY(!q.currentTrack());
        QCOMPARE(q.advance(false), PlayQueue::Step::Moved);
        QCOMPARE(cur(q), 12);
    }

    void removingTheCurrentLastTrackEndsOrMovesOnToTheNextGroup()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Carousel, 1, 10, 2), makeGroup(OriginKind::Carousel, 2, 20, 1)}), false, RepeatMode::Off);
        q.advance(false);  // 11, last of group 1
        q.removeTracks({TrackId(11)});
        QCOMPARE(q.advance(false), PlayQueue::Step::Moved);
        QCOMPARE(cur(q), 20);

        PlayQueue single;
        single.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 1)}), false, RepeatMode::Off);
        QVERIFY(single.removeTracks({TrackId(10)}).currentRemoved);
        QVERIFY(single.isEmpty());
        QCOMPARE(single.advance(false), PlayQueue::Step::End);
    }

    void removingAWholeCurrentGroupMovesToTheNextOne()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Carousel, 1, 10, 2), makeGroup(OriginKind::Carousel, 2, 20, 2)}), false, RepeatMode::Off);
        const auto r = q.removeTracks({TrackId(10), TrackId(11)});
        QVERIFY(r.currentRemoved);
        QCOMPARE(q.groupCount(), 1);
        QCOMPARE(q.advance(false), PlayQueue::Step::Moved);
        QCOMPARE(cur(q), 20);
    }

    void removalAlsoCleansTheHistory()
    {
        PlayQueue q;
        q.start(requestOf({makeGroup(OriginKind::Album, 1, 10, 4)}), false, RepeatMode::Off);
        q.advance(false);
        q.advance(false);  // at 12, history 10, 11
        q.removeTracks({TrackId(11)});
        QVERIFY(q.goPrevious());
        QCOMPARE(cur(q), 10);
    }

    // ---- persistence
    void theQueueSurvivesAJsonRoundTrip()
    {
        PlayQueue q(4);
        q.start(requestOf({makeGroup(OriginKind::Discography, 5, 10, 4), makeGroup(OriginKind::Carousel, 6, 20, 3)}, 0, 2, AfterEnd::Recommend),
                true, RepeatMode::All);
        q.advance(false);
        q.advance(false);
        const auto restored = PlayQueue::fromJson(QJsonDocument::fromJson(QJsonDocument(q.toJson()).toJson()).object());
        QVERIFY(restored.has_value());
        QCOMPARE(restored->currentGroupIndex(), q.currentGroupIndex());
        QCOMPARE(restored->currentTrack()->track, q.currentTrack()->track);
        QCOMPARE(restored->shuffle(), true);
        QCOMPARE(restored->repeat(), RepeatMode::All);
        QCOMPARE(restored->afterEnd(), AfterEnd::Recommend);
        QCOMPARE(restored->currentGroupInOrder(), q.currentGroupInOrder());
        QCOMPARE(restored->canPrevious(), q.canPrevious());
        PlayQueue a = q;
        PlayQueue b = *restored;
        QVERIFY(b.goPrevious() == a.goPrevious());
        QCOMPARE(b.currentTrack()->track, a.currentTrack()->track);
    }

    void brokenJsonIsRejected()
    {
        QVERIFY(!PlayQueue::fromJson({}).has_value());
        QJsonObject bad{{QStringLiteral("groups"), QJsonArray{QJsonObject{{QStringLiteral("kind"), 99}}}}};
        QVERIFY(!PlayQueue::fromJson(bad).has_value());
    }
};

QTEST_GUILESS_MAIN(TstPlayQueue)
#include "tst_play_queue.moc"
