#include "TestLibrary.h"

#include "core/browse/PlayRequestBuilder.h"
#include "core/home/CategoryGenerator.h"
#include "core/home/Greeting.h"
#include "core/home/HomeQueries.h"
#include "core/playback/PlayQueue.h"

using namespace semplice;
using namespace semplice::testing;

namespace {

QDateTime at(int month, int day, int hour)
{
    return QDateTime(QDate(2026, month, day), QTime(hour, 30));
}

// A source with a fixed set of instances per type.
class FakeSource final : public ICategorySource {
public:
    QList<Instance> instances(CategoryType type) override
    {
        QList<Instance> out;
        for (const QString& arg : data.value(type))
            out.append({arg, arg});
        return out;
    }
    QHash<CategoryType, QStringList> data;
};

int distinctTypes(const QList<CategorySpec>& specs)
{
    QSet<int> out;
    for (const auto& s : specs)
        out.insert(static_cast<int>(s.type));
    return static_cast<int>(out.size());
}

}  // namespace

class TstHome : public QObject {
    Q_OBJECT

private slots:
    // ================================================================ greeting
    void anEmptyLibraryAlwaysSaysWelcome()
    {
        QCOMPARE(pickGreeting(at(12, 31, 10), false), GreetingKind::Welcome);
        QCOMPARE(pickGreeting(at(5, 5, 3), false), GreetingKind::Welcome);
    }

    void occasionsBeatTheTimeOfDay_data()
    {
        QTest::addColumn<int>("month");
        QTest::addColumn<int>("day");
        QTest::addColumn<int>("kind");
        QTest::newRow("new year's eve") << 12 << 31 << static_cast<int>(GreetingKind::NewYear);
        QTest::newRow("new year") << 1 << 1 << static_cast<int>(GreetingKind::NewYear);
        QTest::newRow("jan 2") << 1 << 2 << static_cast<int>(GreetingKind::NewYear);
        QTest::newRow("spring") << 3 << 1 << static_cast<int>(GreetingKind::Spring);
        QTest::newRow("summer") << 6 << 1 << static_cast<int>(GreetingKind::Summer);
        QTest::newRow("autumn") << 9 << 1 << static_cast<int>(GreetingKind::Autumn);
        QTest::newRow("winter") << 12 << 1 << static_cast<int>(GreetingKind::Winter);
    }
    void occasionsBeatTheTimeOfDay()
    {
        QFETCH(int, month);
        QFETCH(int, day);
        QFETCH(int, kind);
        for (int hour : {3, 9, 15, 22})
            QCOMPARE(static_cast<int>(pickGreeting(at(month, day, hour), true)), kind);
    }

    void otherDaysFollowTheTimeOfDay_data()
    {
        QTest::addColumn<int>("hour");
        QTest::addColumn<int>("kind");
        QTest::newRow("04") << 4 << static_cast<int>(GreetingKind::Evening);
        QTest::newRow("05") << 5 << static_cast<int>(GreetingKind::Morning);
        QTest::newRow("11") << 11 << static_cast<int>(GreetingKind::Morning);
        QTest::newRow("12") << 12 << static_cast<int>(GreetingKind::Afternoon);
        QTest::newRow("17") << 17 << static_cast<int>(GreetingKind::Afternoon);
        QTest::newRow("18") << 18 << static_cast<int>(GreetingKind::Evening);
        QTest::newRow("23") << 23 << static_cast<int>(GreetingKind::Evening);
        QTest::newRow("00") << 0 << static_cast<int>(GreetingKind::Evening);
    }
    void otherDaysFollowTheTimeOfDay()
    {
        QFETCH(int, hour);
        QFETCH(int, kind);
        QCOMPARE(static_cast<int>(pickGreeting(at(5, 14, hour), true)), kind);
        QCOMPARE(static_cast<int>(pickGreeting(at(2, 1, hour), true)), kind);  // the 1st of a non-season month
    }

    // ================================================================ keys
    void categoryKeysRoundTrip()
    {
        for (const QString& key : {QStringLiteral("artist:12"), QStringLiteral("year:2019"), QStringLiteral("decade:1990"),
                                   QStringLiteral("genre:hip-hop"), QStringLiteral("playlists"), QStringLiteral("albums")}) {
            const auto spec = CategorySpec::fromKey(key);
            QVERIFY2(spec.has_value(), qPrintable(key));
            QCOMPARE(spec->key(), key);
        }
        QVERIFY(!CategorySpec::fromKey(QStringLiteral("nonsense")).has_value());
        QVERIFY(!CategorySpec::fromKey(QStringLiteral("year:")).has_value());
        QVERIFY(!CategorySpec::fromKey(QStringLiteral("recent")).has_value());
    }

    // ================================================================ generator
    void noTypeRepeatsUntilEveryTypeWasUsed()
    {
        FakeSource source;
        for (int t = 0; t <= 3; ++t)
            source.data[static_cast<CategoryType>(t)] = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")};
        source.data[CategoryType::YourPlaylists] = {QString()};
        source.data[CategoryType::YourAlbums] = {QString()};
        for (quint64 seed = 1; seed <= 30; ++seed) {
            CategoryGenerator gen(source, seed);
            const QList<CategorySpec> first = gen.next(6);
            QCOMPARE(first.size(), 6);
            QCOMPARE(distinctTypes(first), 6);  // all six types once
            // the next cycle: the two single-instance types are used up, the other four come once each
            const QList<CategorySpec> second = gen.next(4);
            QCOMPARE(second.size(), 4);
            QCOMPARE(distinctTypes(second), 4);
        }
    }

    void instancesAreNeverShownTwiceAndContentRunsOut()
    {
        FakeSource source;
        source.data[CategoryType::Year] = {QStringLiteral("2001"), QStringLiteral("2002"), QStringLiteral("2003")};
        source.data[CategoryType::YourAlbums] = {QString()};
        CategoryGenerator gen(source, 7);
        const QList<CategorySpec> all = gen.next(10);
        QCOMPARE(all.size(), 4);  // three years and "Your Albums": nothing else exists
        QSet<QString> keys;
        for (const auto& s : all)
            keys.insert(s.key());
        QCOMPARE(keys.size(), 4);
        QVERIFY(gen.next(5).isEmpty());
        gen.reset();
        QCOMPARE(gen.next(10).size(), 4);
    }

    void aTypeWithoutContentIsSkipped()
    {
        FakeSource source;  // nothing at all
        CategoryGenerator gen(source, 1);
        QVERIFY(gen.next(5).isEmpty());
        source.data[CategoryType::Genre] = {QStringLiteral("rock")};
        QCOMPARE(gen.next(5).size(), 1);
    }

    void theSameSeedGivesTheSameCategories()
    {
        FakeSource source;
        for (int t = 0; t <= 3; ++t)
            source.data[static_cast<CategoryType>(t)] = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
        CategoryGenerator one(source, 42), two(source, 42), other(source, 43);
        QList<QString> a, b, c;
        for (const auto& s : one.next(8))
            a << s.key();
        for (const auto& s : two.next(8))
            b << s.key();
        for (const auto& s : other.next(8))
            c << s.key();
        QCOMPARE(a, b);
        QVERIFY(a != c);
    }

    void moreCategoriesContinueTheCycle()
    {
        FakeSource source;
        for (int t = 0; t <= 3; ++t)
            source.data[static_cast<CategoryType>(t)] = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")};
        CategoryGenerator gen(source, 5);
        const auto first = gen.next(5);   // one full cycle of 4 + the first of the next
        QList<CategorySpec> all = first;
        all += gen.next(3);               // lazy loading while scrolling
        QCOMPARE(all.size(), 8);
        // every window of four consecutive categories (aligned to the cycle) holds four different types
        QCOMPARE(distinctTypes(all.mid(0, 4)), 4);
        QCOMPARE(distinctTypes(all.mid(4, 4)), 4);
    }

    // ================================================================ queries on a real library
    void eligibilityFollowsTheMinimums()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("Busy"));
        const ArtistId b = lib.artist(QStringLiteral("Quiet"));
        for (int i = 1; i <= 5; ++i)
            lib.album(a, QStringLiteral("Busy %1").arg(i), 1, {}, {QStringLiteral("rock")}, {1995, 1, 1});
        lib.album(b, QStringLiteral("Quiet 1"), 1, {}, {QStringLiteral("jazz")}, {2001, 1, 1});
        lib.album(b, QStringLiteral("Quiet 2"), 1, {}, {QStringLiteral("rock")}, {2002, 1, 1});
        lib.artist(QStringLiteral("Ghost"), /*hidden=*/true);

        HomeQueries q(*lib.db);
        auto artists = q.instances(CategoryType::ArtistDiscography);
        QCOMPARE(artists.size(), 1);  // only the artist with >= 3 albums
        QCOMPARE(artists.first().label, QStringLiteral("Busy"));
        QCOMPARE(q.instances(CategoryType::Year).size(), 1);       // 1995 has 5 albums
        QCOMPARE(q.instances(CategoryType::Year).first().arg, QStringLiteral("1995"));
        QCOMPARE(q.instances(CategoryType::Decade).size(), 1);     // the 1990s: 5 albums; the 2000s only 2
        QCOMPARE(q.instances(CategoryType::Decade).first().arg, QStringLiteral("1990"));
        QCOMPARE(q.instances(CategoryType::Genre).size(), 1);      // rock: 6 albums; jazz: 1
        QCOMPARE(q.instances(CategoryType::Genre).first().arg, QStringLiteral("rock"));
        QCOMPARE(q.instances(CategoryType::YourAlbums).size(), 1);  // 7 albums
        QCOMPARE(q.instances(CategoryType::YourPlaylists).size(), 0);  // no playlists
    }

    void theUnsortedAlbumIsNeverCounted()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        lib.album(a, QStringLiteral("One"), 1);
        lib.album(a, QStringLiteral("Two"), 1);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        QVERIFY(lib.service->addToDiscography(lib.trackAt(p, 1), a).ok());  // creates the Unsorted album
        HomeQueries q(*lib.db);
        QCOMPARE(q.instances(CategoryType::ArtistDiscography).size(), 0);  // 2 real albums only
        const auto c = q.contents(*CategorySpec::fromKey(QStringLiteral("artist:%1").arg(a.value)), 0);
        QVERIFY(c.ok());
        QCOMPARE(c.value().items.size(), 2);
    }

    void contentsAreCappedAndShuffledForTheCarouselAndCompleteForThePage()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("Prolific"));
        for (int i = 1; i <= 60; ++i)
            lib.album(a, QStringLiteral("Album %1").arg(i), 1, {}, {}, {2000 + i % 20, 1, 1});
        HomeQueries q(*lib.db);
        std::mt19937_64 rng(3);
        const CategorySpec spec{CategoryType::ArtistDiscography, QString::number(a.value), {}};
        const auto carousel = q.contents(spec, CategoryGenerator::kMaxItems, &rng);
        QCOMPARE(carousel.value().items.size(), 50);
        const auto page = q.contents(spec, 0);
        QCOMPARE(page.value().items.size(), 60);  // "all items" for the category page
        // the page lists the discography newest release first
        for (int i = 1; i < page.value().items.size(); ++i)
            QVERIFY(page.value().items.at(i - 1).year >= page.value().items.at(i).year);
        QVERIFY(page.value().durationMs > 0);
        // the carousel is a random order, not the page order
        QList<qint64> carouselIds, pageIds;
        for (const auto& it : carousel.value().items.mid(0, 10))
            carouselIds << it.id;
        for (const auto& it : page.value().items.mid(0, 10))
            pageIds << it.id;
        QVERIFY(carouselIds != pageIds);
    }

    void yourPlaylistsAndRecentlyPlayed()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("Rec"), 1);
        lib.clock.advance(1000);
        const PlaylistId p1 = lib.playlistOfNew(QStringLiteral("One"), a, 1);
        lib.clock.advance(1000);
        const PlaylistId p2 = lib.playlistOfNew(QStringLiteral("Two"), a, 1);
        HomeQueries q(*lib.db);
        const auto playlists = q.contents(CategorySpec{CategoryType::YourPlaylists, {}, {}}, 0);
        QCOMPARE(playlists.value().items.size(), 2);
        QCOMPARE(playlists.value().items.first().title, QStringLiteral("Two"));  // most recent first
        QCOMPARE(q.instances(CategoryType::YourPlaylists).size(), 1);

        RecentRepo recent(*lib.db);
        QVERIFY(recent.touch(ContainerKind::Album, album.value, 100).ok());
        QVERIFY(recent.touch(ContainerKind::Playlist, p1.value, 200).ok());
        QVERIFY(recent.touch(ContainerKind::Album, album.value, 300).ok());  // moves to the front, no duplicate
        const auto items = q.recent();
        QVERIFY(items.ok());
        QCOMPARE(items.value().size(), 2);
        QCOMPARE(items.value().at(0).kind, ContainerKind::Album);
        QCOMPARE(items.value().at(0).title, QStringLiteral("Rec"));
        QCOMPARE(items.value().at(0).subtitle, QStringLiteral("A"));
        QCOMPARE(items.value().at(1).kind, ContainerKind::Playlist);
        QVERIFY(items.value().at(1).subtitle.isEmpty());  // playlists show only the title
        Q_UNUSED(p2)
    }

    void everyArtistOfAnAlbumIsAvailableAsALink()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("Freddie"));
        const ArtistId b = lib.artist(QStringLiteral("JAK3"));
        const ArtistId c = lib.artist(QStringLiteral("Third"));
        const AlbumId album = lib.album(a, QStringLiteral("Casper"), 1);
        AlbumRepo repo(*lib.db);
        QVERIFY(repo.setCoAuthors(album, {{b, 1}, {c, 2}}).ok());
        RecentRepo recent(*lib.db);
        QVERIFY(recent.touch(ContainerKind::Album, album.value, 100).ok());
        HomeQueries q(*lib.db);
        const auto items = q.recent();
        QVERIFY(items.ok());
        QCOMPARE(items.value().size(), 1);
        const auto owners = items.value().first().owners;
        QCOMPARE(owners.size(), 3);
        QCOMPARE(owners.at(0).id, a.value);  // the primary artist first
        QCOMPARE(owners.at(1).name, QStringLiteral("JAK3"));
        QCOMPARE(owners.at(2).id, c.value);
        QCOMPARE(items.value().first().subtitle, QStringLiteral("Freddie, JAK3, Third"));
    }

    void recentlyPlayedKeepsOnlyExistingItemsAndAtMostFifty()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        RecentRepo recent(*lib.db);
        AlbumId last;
        for (int i = 1; i <= 55; ++i) {
            last = lib.album(a, QStringLiteral("Album %1").arg(i), 1);
            QVERIFY(recent.touch(ContainerKind::Album, last.value, i).ok());
        }
        HomeQueries q(*lib.db);
        QCOMPARE(q.recent().value().size(), 50);
        QCOMPARE(q.recent().value().first().id, last.value);
        QVERIFY(lib.service->deleteAlbum(last).ok());  // deleting clears the recent entry too (M1 rule)
        QCOMPARE(q.recent().value().first().title, QStringLiteral("Album 54"));
    }

    void hasContentDecidesTheGreeting()
    {
        TestLibrary lib;
        HomeQueries q(*lib.db);
        QVERIFY(!q.hasContent().value());
        const ArtistId a = lib.artist(QStringLiteral("A"));
        QVERIFY(!q.hasContent().value());  // an artist alone is not content
        lib.album(a, QStringLiteral("One"), 1);
        QVERIFY(q.hasContent().value());
    }

    // ================================================================ carousel queues
    void startingFromACarouselMakesOneGroupPerItem()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId one = lib.album(a, QStringLiteral("One"), 2);
        const PlaylistId pl = lib.playlistOfNew(QStringLiteral("Mix"), a, 2);
        const AlbumId two = lib.album(a, QStringLiteral("Two"), 3);
        PlayRequestBuilder builder(*lib.db);
        const QList<ContainerRef> items{{ContainerKind::Album, one.value}, {ContainerKind::Playlist, pl.value}, {ContainerKind::Album, two.value}};
        const PlayRequest r = builder.carouselFrom(items, 1).value();
        QCOMPARE(r.groups.size(), 3);
        QCOMPARE(r.startGroup, 1);
        QCOMPARE(r.afterEnd, AfterEnd::Recommend);
        QCOMPARE(r.groups.at(0).origin.kind, OriginKind::Album);
        QCOMPARE(r.groups.at(1).origin.kind, OriginKind::Playlist);
        QCOMPARE(r.groups.at(2).tracks.size(), 3);

        // an item that vanished is left out and the start still points at the right one
        QVERIFY(lib.service->deleteAlbum(one).ok());
        const PlayRequest r2 = builder.carouselFrom(items, 2).value();
        QCOMPARE(r2.groups.size(), 2);
        QCOMPARE(r2.startGroup, 1);
        QCOMPARE(r2.groups.at(r2.startGroup).origin.id, two.value);
        QVERIFY(!builder.carouselFrom({{ContainerKind::Album, 9999}}, 0).ok());
    }

    void thePlayButtonOfACategoryIsOneGroupWithSources()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId one = lib.album(a, QStringLiteral("One"), 2);
        const PlaylistId pl = lib.playlistOfNew(QStringLiteral("Mix"), a, 1);
        PlayRequestBuilder builder(*lib.db);
        const PlayRequest r = builder.carouselAll({{ContainerKind::Album, one.value}, {ContainerKind::Playlist, pl.value}},
                                                  QStringLiteral("Year 2020"), QStringLiteral("year:2020")).value();
        QCOMPARE(r.groups.size(), 1);
        const QueueGroup& g = r.groups.first();
        QCOMPARE(g.origin.kind, OriginKind::Carousel);
        QCOMPARE(g.origin.title, QStringLiteral("Year 2020"));
        QCOMPARE(g.origin.key, QStringLiteral("year:2020"));
        QCOMPARE(g.tracks.size(), 3);
        QCOMPARE(g.tracks.at(0).sourceId, one.value);
        QCOMPARE(g.tracks.at(2).sourceKind, ContainerKind::Playlist);
        QCOMPARE(g.tracks.at(2).sourceId, pl.value);
    }

    void theCarouselKeySurvivesTheSessionJson()
    {
        PlayRequest request;
        QueueGroup g;
        g.origin = {OriginKind::Carousel, 0, QStringLiteral("Year 2020"), QStringLiteral("year:2020")};
        g.tracks = {{TrackId(1)}};
        request.groups = {g};
        PlayQueue q;
        QVERIFY(q.start(request, false, RepeatMode::Off));
        const auto restored = PlayQueue::fromJson(q.toJson());
        QVERIFY(restored.has_value());
        QCOMPARE(restored->currentOrigin().key, QStringLiteral("year:2020"));
    }
};

QTEST_GUILESS_MAIN(TstHome)
#include "tst_home.moc"
