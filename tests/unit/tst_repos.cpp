
#include "core/db/AlbumRepo.h"
#include "core/db/ArtistRepo.h"
#include "core/db/Migrator.h"
#include "core/db/PlaylistRepo.h"
#include "core/db/TagRepo.h"
#include "core/db/TrackRepo.h"

#include <QtTest>

using namespace semplice;

class TstRepos : public QObject {
    Q_OBJECT
private:
    static std::unique_ptr<Database> open()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        const auto migrated = Migrator::migrate(*db);
        if (!migrated.ok())
            qFatal("migration failed");
        Q_UNUSED(migrated);
        return db;
    }

    static Artist artist(const QString& name, const QString& dir)
    {
        Artist a;
        a.name = name;
        a.dirName = dir;
        a.createdAt = a.updatedAt = 1;
        return a;
    }

    static Track track(const QString& title, const QString& path, int seed, HomeRef home)
    {
        Track t;
        t.title = title;
        t.filePath = path;
        t.contentHash = QByteArray(32, static_cast<char>(seed));
        t.home = home;
        t.createdAt = t.updatedAt = 1;
        return t;
    }

private slots:
    void artistRoundTripAndDuplicateName()
    {
        auto db = open();
        ArtistRepo repo(*db);
        const ArtistId id = repo.insert(artist(QStringLiteral("Ёлка"), QStringLiteral("Ёлка"))).value();
        QVERIFY(id.isValid());
        const Artist back = *repo.get(id).value();
        QCOMPARE(back.name, QStringLiteral("Ёлка"));
        QCOMPARE(back.dirName, QStringLiteral("Ёлка"));
        QVERIFY(!back.hidden);

        QVERIFY(repo.findByName(QStringLiteral("ёЛКА")).value().has_value());
        const auto dup = repo.insert(artist(QStringLiteral("  ёлка"), QStringLiteral("other")));
        QVERIFY(!dup.ok());
        QCOMPARE(dup.error().code, ErrorCode::AlreadyExists);
    }

    void hiddenArtistsMustNotHaveAFolderRuleIsOneWay()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        ArtistRepo repo(*db);
        Artist hidden = artist(QStringLiteral("H"), QString());
        hidden.hidden = true;
        QVERIFY(repo.insert(hidden).ok());
        Artist visibleWithoutFolder = artist(QStringLiteral("V"), QString());
        QVERIFY(!repo.insert(visibleWithoutFolder).ok());  // CHECK (hidden = 1 OR dir_name IS NOT NULL)
    }

    void albumRoundTripAndPerArtistUniqueness()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        ArtistRepo artists(*db);
        AlbumRepo albums(*db);
        const ArtistId a = artists.insert(artist(QStringLiteral("A"), QStringLiteral("A"))).value();
        const ArtistId b = artists.insert(artist(QStringLiteral("B"), QStringLiteral("B"))).value();

        Album album;
        album.primaryArtist = a;
        album.title = QStringLiteral("Gold");
        album.dirName = QStringLiteral("Gold");
        album.type = AlbumType::Live;
        album.date = {2001, 2, 3};
        album.createdAt = album.updatedAt = 1;
        const AlbumId id = albums.insert(album).value();
        const Album back = *albums.get(id).value();
        QCOMPARE(back.type, AlbumType::Live);
        QCOMPARE(back.date, (ReleaseDate{2001, 2, 3}));

        Album sameTitle = album;
        sameTitle.title = QStringLiteral("GOLD");
        sameTitle.dirName = QStringLiteral("Gold 2");
        QCOMPARE(albums.insert(sameTitle).error().code, ErrorCode::AlreadyExists);
        sameTitle.primaryArtist = b;  // another artist may reuse the title
        QVERIFY(albums.insert(sameTitle).ok());

        Album yearOnly = album;
        yearOnly.title = QStringLiteral("Year only");
        yearOnly.dirName = QStringLiteral("Year only");
        yearOnly.date = {1999, 0, 0};
        const Album yearBack = *albums.get(albums.insert(yearOnly).value()).value();
        QCOMPARE(yearBack.date.month, 0);
        QCOMPARE(yearBack.date.day, 0);

        Album badDate = album;
        badDate.title = QStringLiteral("Bad");
        badDate.dirName = QStringLiteral("Bad");
        badDate.date = {2000, 0, 5};  // day without month
        QVERIFY(!albums.insert(badDate).ok());
    }

    void membershipOrderInsertAndRenumber()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        ArtistRepo artists(*db);
        AlbumRepo albums(*db);
        TrackRepo tracks(*db);
        const ArtistId a = artists.insert(artist(QStringLiteral("A"), QStringLiteral("A"))).value();
        Album album;
        album.primaryArtist = a;
        album.title = QStringLiteral("T");
        album.dirName = QStringLiteral("T");
        album.date = {2000, 0, 0};
        album.createdAt = album.updatedAt = 1;
        const AlbumId id = albums.insert(album).value();

        QList<TrackId> ids;
        for (int i = 1; i <= 3; ++i) {
            ids << tracks
                       .insert(track(QStringLiteral("t%1").arg(i), QStringLiteral("p%1").arg(i), i,
                                     {ContainerKind::Album, id.value}))
                       .value();
            QVERIFY(albums.addTrack(id, ids.last(), i, 1).ok());
        }
        // Insert in the middle: later tracks shift down.
        const TrackId inserted = tracks.insert(track(QStringLiteral("mid"), QStringLiteral("pm"), 9,
                                                     {ContainerKind::Album, id.value}))
                                     .value();
        QVERIFY(albums.addTrack(id, inserted, 2, 1).ok());
        QCOMPARE(albums.tracks(id).value(), (QList<TrackId>{ids[0], inserted, ids[1], ids[2]}));

        QVERIFY(albums.removeTrack(id, ids[0]).ok());
        QVERIFY(albums.renumber(id).ok());
        QCOMPARE(albums.positionOf(id, inserted).value(), 1);
        QCOMPARE(albums.positionOf(id, ids[2]).value(), 3);
    }

    void trackHashAndPathAreUnique()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        TrackRepo tracks(*db);
        QVERIFY(tracks.insert(track(QStringLiteral("a"), QStringLiteral("x/A.mp3"), 1, {ContainerKind::Playlist, 1})).ok());
        QCOMPARE(tracks.insert(track(QStringLiteral("b"), QStringLiteral("x/a.mp3"), 2, {ContainerKind::Playlist, 1}))
                     .error()
                     .code,
                 ErrorCode::AlreadyExists);  // same path, other case
        QCOMPARE(tracks.insert(track(QStringLiteral("c"), QStringLiteral("x/c.mp3"), 1, {ContainerKind::Playlist, 1}))
                     .error()
                     .code,
                 ErrorCode::AlreadyExists);  // same content hash
        QVERIFY(tracks.findByHash(QByteArray(32, 1)).value().has_value());
        QVERIFY(tracks.findByPath(QStringLiteral("X/A.MP3")).value().has_value());
    }

    void deletingATrackCascadesCreditsTagsAndMemberships()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        ArtistRepo artists(*db);
        TrackRepo tracks(*db);
        TagRepo tags(*db);
        const ArtistId a = artists.insert(artist(QStringLiteral("A"), QStringLiteral("A"))).value();
        const TrackId t = tracks.insert(track(QStringLiteral("t"), QStringLiteral("p"), 1, {ContainerKind::Playlist, 1})).value();
        QVERIFY(tracks.setCredits(t, {{a, 0, CreditRole::Feat}}).ok());
        QVERIFY(tags.setTags(TagOwner::Track, t.value, {QStringLiteral("rock"), QStringLiteral("pop")}).ok());
        QCOMPARE(tags.tags(TagOwner::Track, t.value).value(), (QStringList{QStringLiteral("pop"), QStringLiteral("rock")}));
        QCOMPARE(tracks.creditCount(a).value(), 1);
        QVERIFY(tracks.remove(t).ok());
        QCOMPARE(tracks.creditCount(a).value(), 0);
        QVERIFY(tags.tags(TagOwner::Track, t.value).value().isEmpty());
    }

    void artistsReferencedByCreditsCannotBeDeletedDirectly()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        ArtistRepo artists(*db);
        TrackRepo tracks(*db);
        const ArtistId a = artists.insert(artist(QStringLiteral("A"), QStringLiteral("A"))).value();
        const TrackId t = tracks.insert(track(QStringLiteral("t"), QStringLiteral("p"), 1, {ContainerKind::Playlist, 1})).value();
        QVERIFY(tracks.setCredits(t, {{a, 0, CreditRole::Main}}).ok());
        QVERIFY(!artists.remove(a).ok());  // ON DELETE RESTRICT: foreign keys are really on
    }

    void recentListKeepsFiftyNewestWithoutDuplicates()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        RecentRepo recent(*db);
        for (int i = 1; i <= 60; ++i)
            QVERIFY(recent.touch(ContainerKind::Album, i, i).ok());
        QVERIFY(recent.touch(ContainerKind::Album, 20, 100).ok());  // replay moves to the front
        const QList<Membership> list = recent.list().value();
        QCOMPARE(list.size(), 50);
        QCOMPARE(list.first().containerId, qint64(20));
        QCOMPARE(list.at(1).containerId, qint64(60));
        QCOMPARE(list.last().containerId, qint64(11));  // ids 1..10 fell off the end (20 is still in)
        QSet<qint64> seen;
        for (const Membership& m : list)
        {
            QVERIFY(!seen.contains(m.containerId));
            seen.insert(m.containerId);
        }
    }

    void kvStoreOverwrites()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        KvRepo kv(*db);
        QVERIFY(!kv.get(QStringLiteral("session")).value().has_value());
        QVERIFY(kv.set(QStringLiteral("session"), QStringLiteral("a")).ok());
        QVERIFY(kv.set(QStringLiteral("session"), QStringLiteral("b")).ok());
        QCOMPARE(*kv.get(QStringLiteral("session")).value(), QStringLiteral("b"));
        QVERIFY(kv.remove(QStringLiteral("session")).ok());
        QVERIFY(!kv.get(QStringLiteral("session")).value().has_value());
    }

    void transactionRollsBackUnlessCommitted()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        KvRepo kv(*db);
        {
            Transaction tx(*db);
            QVERIFY(tx.begin().ok());
            QVERIFY(kv.set(QStringLiteral("k"), QStringLiteral("v")).ok());
        }  // no commit
        QVERIFY(!kv.get(QStringLiteral("k")).value().has_value());
        {
            Transaction tx(*db);
            QVERIFY(tx.begin().ok());
            QVERIFY(kv.set(QStringLiteral("k"), QStringLiteral("v")).ok());
            QVERIFY(tx.commit().ok());
        }
        QVERIFY(kv.get(QStringLiteral("k")).value().has_value());
    }
};

QTEST_GUILESS_MAIN(TstRepos)
#include "tst_repos.moc"
