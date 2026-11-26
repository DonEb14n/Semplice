#include "TestLibrary.h"

#include "core/recommend/LibraryRecommendationSource.h"
#include "core/recommend/Recommender.h"

using namespace semplice;
using namespace semplice::testing;

namespace {

// a fixed sequence of "random" numbers: tests choose what the weighted draw returns
class FixedRandom final : public IRandom {
public:
    explicit FixedRandom(QList<double> values = {0.5}) : m_values(std::move(values)) {}
    double next01() override
    {
        const double v = m_values.at(m_next % m_values.size());
        ++m_next;
        return v;
    }

private:
    QList<double> m_values;
    int m_next = 0;
};

TrackFeatures feat(qint64 id, qint64 album, QList<int> tags = {}, QList<qint64> artists = {}, int year = 2000, qint64 lastPlayed = 0)
{
    TrackFeatures f;
    f.id = TrackId(id);
    f.album = album;
    if (album > 0)
        f.albums = {album};
    f.tags = std::move(tags);
    f.artists = std::move(artists);
    f.year = year;
    f.lastPlayedMs = lastPlayed;
    return f;
}

TagRelations relations(int n = 6)
{
    TagRelations r;
    r.related.resize(n);
    r.opposite.resize(n);
    r.related[0] = {1};   // 0 <-> 1 related
    r.related[1] = {0};
    r.opposite[0] = {2};  // 0 <-> 2 opposite
    r.opposite[2] = {0};
    return r;
}

const qint64 kNow = 10LL * 24 * 3600 * 1000;

SeedProfile seedWith(QList<int> tags, QList<qint64> artists = {}, int year = 2000)
{
    TrackFeatures s = feat(1, 100, std::move(tags), std::move(artists), year);
    return SeedProfile::of({&s});
}

}  // namespace

class TstRecommender : public QObject {
    Q_OBJECT

    static QList<qint64> albumsOf(const QList<TrackId>& ids, const QList<TrackFeatures>& pool)
    {
        QList<qint64> out;
        for (TrackId id : ids)
            for (const TrackFeatures& f : pool)
                if (f.id == id)
                    out.append(f.album);
        return out;
    }

private slots:
    // ---- the scorer
    void sharedTagsScoreByOverlap()
    {
        const RecommenderConfig c;
        const SeedProfile seed = seedWith({3, 4});
        const ScoreParts same = scoreTrack(feat(2, 1, {3, 4}), seed, relations(), kNow, c);
        const ScoreParts half = scoreTrack(feat(2, 1, {3, 5}), seed, relations(), kNow, c);
        const ScoreParts none = scoreTrack(feat(2, 1, {5}), seed, relations(), kNow, c);
        QCOMPARE(same.tags, 0.5);
        QVERIFY(qFuzzyCompare(half.tags, 0.5 / 3.0));
        QCOMPARE(none.tags, 0.0);
    }
    void sameArtistAddsItsWeightAlsoForFeats()
    {
        const SeedProfile seed = seedWith({}, {7});
        QCOMPARE(scoreTrack(feat(2, 1, {}, {9, 7}), seed, relations(), kNow, {}).artist, 0.30);
        QCOMPARE(scoreTrack(feat(2, 1, {}, {9}), seed, relations(), kNow, {}).artist, 0.0);
    }
    void relatedGenreAddsAndOppositeSubtracts()
    {
        const SeedProfile seed = seedWith({0});
        QCOMPARE(scoreTrack(feat(2, 1, {1}), seed, relations(), kNow, {}).related, 0.20);
        QCOMPARE(scoreTrack(feat(2, 1, {2}), seed, relations(), kNow, {}).opposite, -0.30);
        QCOMPARE(scoreTrack(feat(2, 1, {3}, {}, 0), seed, relations(), kNow, {}).total(), 0.0);
        QVERIFY(scoreTrack(feat(2, 1, {1}), seed, relations(), kNow, {}).total() > scoreTrack(feat(2, 1, {2}), seed, relations(), kNow, {}).total());
    }
    void yearProximityFadesOverTwentyYears()
    {
        const SeedProfile seed = seedWith({}, {}, 2000);
        QCOMPARE(scoreTrack(feat(2, 1, {}, {}, 2000), seed, relations(), kNow, {}).year, 0.10);
        QVERIFY(qFuzzyCompare(scoreTrack(feat(2, 1, {}, {}, 2010), seed, relations(), kNow, {}).year, 0.05));
        QCOMPARE(scoreTrack(feat(2, 1, {}, {}, 2030), seed, relations(), kNow, {}).year, 0.0);
        QCOMPARE(scoreTrack(feat(2, 1, {}, {}, 0), seed, relations(), kNow, {}).year, 0.0);
    }
    void recentlyPlayedPenaltyDecaysOverSevenDays()
    {
        const SeedProfile seed = seedWith({});
        const qint64 day = 24LL * 3600 * 1000;
        QCOMPARE(scoreTrack(feat(2, 1, {}, {}, 2000, kNow), seed, relations(), kNow, {}).recent, -0.40);
        QVERIFY(qFuzzyCompare(scoreTrack(feat(2, 1, {}, {}, 2000, kNow - day * 7 / 2), seed, relations(), kNow, {}).recent, -0.20));
        QCOMPARE(scoreTrack(feat(2, 1, {}, {}, 2000, kNow - day * 8), seed, relations(), kNow, {}).recent, 0.0);
        QCOMPARE(scoreTrack(feat(2, 1, {}, {}, 2000, 0), seed, relations(), kNow, {}).recent, 0.0);
    }

    // ---- the selector
    void theExcludeListAndSeedAlbumsAreLeftOut()
    {
        QList<TrackFeatures> pool{feat(1, 100), feat(2, 100), feat(3, 200), feat(4, 300), feat(5, 400)};
        FixedRandom rnd;
        Selector s({}, rnd);
        s.setPool(pool, relations());
        PickRequest r;
        r.seeds = {TrackId(1)};
        r.exclude = {TrackId(3)};
        r.count = 3;
        r.nowMs = kNow;
        const QList<TrackId> got = s.pick(r);
        QCOMPARE(got.size(), 2);  // only 4 and 5 are left outside the seed album and the exclude list
        QVERIFY(!got.contains(TrackId(1)) && !got.contains(TrackId(2)) && !got.contains(TrackId(3)));
    }
    void neverTheSameAlbumTwiceInARowAndAtMostTwoPerWindow()
    {
        QList<TrackFeatures> pool;
        qint64 id = 10;
        for (qint64 album = 1; album <= 3; ++album)
            for (int i = 0; i < 12; ++i)
                pool.append(feat(id++, album));
        pool.append(feat(1, 99));
        FixedRandom rnd({0.1, 0.9, 0.4, 0.7, 0.2});
        Selector s({}, rnd);
        s.setPool(pool, relations());
        QList<TrackId> all;
        for (int round = 0; round < 6; ++round) {
            PickRequest r;
            r.seeds = {TrackId(1)};
            r.count = 3;
            r.nowMs = kNow;
            all += s.pick(r);
        }
        const QList<qint64> albums = albumsOf(all, pool);
        QCOMPARE(albums.size(), 18);
        for (int i = 1; i < albums.size(); ++i)
            QVERIFY2(albums.at(i) != albums.at(i - 1), "same album twice in a row");
    }
    void atMostTwoPerAlbumWhenThePoolIsWideEnough()
    {
        QList<TrackFeatures> pool;
        qint64 id = 10;
        for (qint64 album = 1; album <= 8; ++album)
            for (int i = 0; i < 6; ++i)
                pool.append(feat(id++, album));
        pool.append(feat(1, 99));
        FixedRandom rnd({0.05, 0.95, 0.5, 0.3, 0.8, 0.15, 0.65});
        Selector s({}, rnd);
        s.setPool(pool, relations());
        PickRequest r;
        r.seeds = {TrackId(1)};
        r.count = 10;
        r.nowMs = kNow;
        const QList<qint64> albums = albumsOf(s.pick(r), pool);
        QCOMPARE(albums.size(), 10);
        QHash<qint64, int> n;
        for (qint64 a : albums)
            ++n[a];
        for (int v : n)
            QVERIFY2(v <= 2, "more than two tracks of one album in 10 positions");
    }
    void noRepeatsUntilThePoolIsExhausted()
    {
        QList<TrackFeatures> pool;
        for (qint64 i = 1; i <= 12; ++i)
            pool.append(feat(i, 0));  // playlist-only tracks: no album rules
        FixedRandom rnd({0.3, 0.6, 0.9, 0.1});
        Selector s({}, rnd);
        s.setPool(pool, relations());
        QSet<qint64> seen;
        for (int round = 0; round < 4; ++round) {
            PickRequest r;
            r.seeds = {TrackId(1)};
            r.exclude = {TrackId(1)};
            r.count = 3;
            r.nowMs = kNow;
            for (TrackId t : s.pick(r)) {
                QVERIFY2(!seen.contains(t.value), "a track came twice before the pool was used up");
                seen.insert(t.value);
            }
        }
        QCOMPARE(seen.size(), 11);  // everything but the excluded one, exactly once
    }
    void seedAlbumsAreAllowedWhenNothingElseIsLeft()
    {
        QList<TrackFeatures> pool{feat(1, 100), feat(2, 100), feat(3, 100), feat(4, 100)};
        FixedRandom rnd;
        Selector s({}, rnd);
        s.setPool(pool, relations());
        PickRequest r;
        r.seeds = {TrackId(1)};
        r.exclude = {TrackId(1)};
        r.count = 2;
        r.nowMs = kNow;
        QCOMPARE(s.pick(r).size(), 2);  // relaxation 1: the seed album itself
    }
    void usedTracksComeBackOnlyWhenEverythingWasUsed()
    {
        QList<TrackFeatures> pool{feat(1, 0), feat(2, 0), feat(3, 0)};
        FixedRandom rnd;
        Selector s({}, rnd);
        s.setPool(pool, relations());
        PickRequest r;
        r.seeds = {TrackId(1)};
        r.exclude = {TrackId(1)};
        r.count = 2;
        r.nowMs = kNow;
        QCOMPARE(s.pick(r).size(), 2);  // 2 and 3
        QCOMPARE(s.pick(r).size(), 2);  // the used set restarts instead of stopping
    }
    void fewerThanTwoPlayableTracksStops()
    {
        FixedRandom rnd;
        Selector s({}, rnd);
        s.setPool({feat(1, 0), feat(2, 0)}, relations());
        PickRequest r;
        r.seeds = {TrackId(1)};
        r.exclude = {TrackId(1)};
        r.count = 3;
        r.nowMs = kNow;
        QVERIFY(s.pick(r).isEmpty());  // only one candidate left
        Selector empty({}, rnd);
        QVERIFY(empty.pick(r).isEmpty());
    }
    void betterScoresAreChosenMoreOften()
    {
        // 0.0 -> the best option, 0.999 -> the worst of the weighted list
        QList<TrackFeatures> pool{feat(1, 100, {3}), feat(2, 200, {3}), feat(3, 300, {5}), feat(4, 400, {5})};
        FixedRandom first({0.0});
        Selector a({}, first);
        a.setPool(pool, relations());
        PickRequest r;
        r.seeds = {TrackId(1)};
        r.count = 1;
        r.nowMs = kNow;
        QCOMPARE(a.pick(r).first(), TrackId(2));  // shares the tag with the seed
        FixedRandom last({0.999});
        Selector b({}, last);
        b.setPool(pool, relations());
        QVERIFY(b.pick(r).first() != TrackId(2));
    }
    void sameSeedAndSameRandomGiveTheSameAnswer()
    {
        QList<TrackFeatures> pool;
        for (qint64 i = 1; i <= 30; ++i)
            pool.append(feat(i, i % 5 + 1, {int(i % 4)}));
        PickRequest r;
        r.seeds = {TrackId(1)};
        r.count = 5;
        r.nowMs = kNow;
        MersenneRandom a(42), b(42);
        Selector sa({}, a), sb({}, b);
        sa.setPool(pool, relations());
        sb.setPool(pool, relations());
        QCOMPARE(sa.pick(r), sb.pick(r));
    }
    void topShowsTheComponents()
    {
        QList<TrackFeatures> pool{feat(1, 100, {0}), feat(2, 200, {1}), feat(3, 300, {2}), feat(4, 400, {0})};
        FixedRandom rnd;
        Selector s({}, rnd);
        s.setPool(pool, relations());
        PickRequest r;
        r.seeds = {TrackId(1)};
        r.nowMs = kNow;
        const auto top = s.top(r, 10);
        QCOMPARE(top.size(), 3);
        QCOMPARE(top.first().id, TrackId(4));      // shares tag 0
        QCOMPARE(top.last().id, TrackId(3));       // tag 2 is the opposite genre
        QVERIFY(top.last().parts.opposite < 0);
    }

    // ---- reading the library
    void theLoaderReadsAlbumsCreditsTagsAndYears()
    {
        TestLibrary lib;
        auto genres = GenreVocabulary::load();
        QVERIFY(genres.ok());
        const ArtistId band = lib.artist(QStringLiteral("Band"));
        const AlbumId album = lib.album(band, QStringLiteral("First"), 3, {}, {QStringLiteral("rock")}, {1999, 1, 1});
        const TrackId t = lib.trackAt(album, 1);
        auto pool = loadRecommendationPool(*lib.db, genres.value());
        QVERIFY(pool.ok());
        QCOMPARE(pool.value().tracks.size(), 3);
        const TrackFeatures* f = nullptr;
        for (const TrackFeatures& x : pool.value().tracks)
            if (x.id == t)
                f = &x;
        QVERIFY(f);
        QCOMPARE(f->album, album.value);
        QCOMPARE(f->year, 1999);
        QCOMPARE(f->artists, QList<qint64>{band.value});
        QCOMPARE(f->tags.size(), 1);  // the album's tag
    }
};

QTEST_MAIN(TstRecommender)
#include "tst_recommender.moc"
