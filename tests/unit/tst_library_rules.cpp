#include "TestLibrary.h"

#include <QSignalSpy>

using namespace semplice;
using namespace semplice::testing;

Q_DECLARE_METATYPE(semplice::ChangeSet)

// Every test builds a fresh in-memory library and ends with the integrity checker, so a rule that
// is "almost right" cannot hide behind a passing assertion.
class TstLibraryRules : public QObject {
    Q_OBJECT

private:
    template <class T>
    static ErrorCode code(const Result<T>& r)
    {
        return r.ok() ? ErrorCode::None : r.error().code;
    }

private slots:
    // ================================================================ uniqueness (SPEC 5.1)
    void artistNamesAreUniqueIgnoringCaseSpacingAndNormalisation()
    {
        TestLibrary lib;
        lib.artist(QStringLiteral("ABBA"));
        QCOMPARE(code(lib.service->createArtist({QStringLiteral("abba")})), ErrorCode::AlreadyExists);
        QCOMPARE(code(lib.service->createArtist({QStringLiteral("  ABBA  ")})), ErrorCode::AlreadyExists);

        lib.artist(QStringLiteral("Ёлка"));
        QCOMPARE(code(lib.service->createArtist({QStringLiteral("ёлка")})), ErrorCode::AlreadyExists);

        lib.artist(QStringLiteral("Café"));  // precomposed
        QCOMPARE(code(lib.service->createArtist({QStringLiteral("Café")})), ErrorCode::AlreadyExists);  // NFD

        // Diacritics make a different name: only search ignores them.
        QVERIFY(lib.service->createArtist({QStringLiteral("Cafe")}).ok());

        QCOMPARE(code(lib.service->createArtist({QStringLiteral("   ")})), ErrorCode::NameEmpty);
        QCOMPARE(lib.integrity(), QString());
    }

    void albumTitlesAreUniquePerPrimaryArtist()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId b = lib.artist(QStringLiteral("B"));
        lib.album(a, QStringLiteral("Gold"), 2);

        AlbumDraft dup;
        dup.primaryArtist = a;
        dup.title = QStringLiteral("GOLD");
        dup.date = {2020, 0, 0};
        TrackInput t;
        t.draft = lib.draft(QStringLiteral("x"));
        dup.tracks = {t};
        QCOMPARE(code(lib.service->createAlbum(dup)), ErrorCode::AlreadyExists);

        QVERIFY(lib.album(b, QStringLiteral("Gold"), 1).isValid());  // other artist: fine
        QCOMPARE(lib.integrity(), QString());
    }

    void playlistNamesAreUnique()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        lib.playlistOfNew(QStringLiteral("Road trip"), a, 1);
        PlaylistDraft dup;
        dup.name = QStringLiteral("road TRIP");
        TrackInput t;
        t.draft = lib.draft(QStringLiteral("x"), {{a, CreditRole::Main}});
        dup.tracks = {t};
        QCOMPARE(code(lib.service->createPlaylist(dup)), ErrorCode::AlreadyExists);
        QCOMPARE(lib.integrity(), QString());
    }

    void unsortedTitleIsReserved()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        for (const QString& title : {QStringLiteral("Unsorted tracks"), QStringLiteral("UNSORTED  Tracks")}) {
            AlbumDraft d;
            d.primaryArtist = a;
            d.title = title;
            d.date = {2020, 0, 0};
            TrackInput t;
            t.draft = lib.draft(QStringLiteral("x"));
            d.tracks = {t};
            QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::ReservedName);
        }
    }

    // ================================================================ album / playlist validity
    void albumNeedsArtistAndTracksAndTitle()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        AlbumDraft d;
        d.primaryArtist = a;
        d.title = QStringLiteral("Empty");
        d.date = {2020, 0, 0};
        QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::NoTracks);

        TrackInput t;
        t.draft = lib.draft(QStringLiteral("x"));
        d.tracks = {t};
        d.primaryArtist = ArtistId();
        QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::NoArtist);
        d.primaryArtist = ArtistId(9999);
        QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::NoArtist);
        d.primaryArtist = a;
        d.title = QStringLiteral(" ");
        QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::NameEmpty);
        QCOMPARE(lib.integrity(), QString());
    }

    void playlistNeedsTracksAndArtists()
    {
        TestLibrary lib;
        QCOMPARE(code(lib.service->createPlaylist({QStringLiteral("P"), {}, {}})), ErrorCode::NoTracks);
        PlaylistDraft d;
        d.name = QStringLiteral("P");
        TrackInput t;
        t.draft = lib.draft(QStringLiteral("no artist"));
        d.tracks = {t};
        QCOMPARE(code(lib.service->createPlaylist(d)), ErrorCode::NoArtist);
    }

    void invalidDateIsRejected()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        AlbumDraft d;
        d.primaryArtist = a;
        d.title = QStringLiteral("T");
        TrackInput t;
        t.draft = lib.draft(QStringLiteral("x"));
        d.tracks = {t};
        for (const ReleaseDate bad : {ReleaseDate{1743, 0, 0}, ReleaseDate{2028, 0, 0}, ReleaseDate{2020, 0, 5},
                                      ReleaseDate{2021, 2, 29}, ReleaseDate{2020, 13, 1}, ReleaseDate{0, 0, 0}}) {
            d.date = bad;
            QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::InvalidDate);
        }
        d.date = {2027, 1, 1};  // current year (2026) + 1 is allowed
        QVERIFY(lib.service->createAlbum(d).ok());
    }

    void tagKeysMustBeInTheVocabulary()
    {
        TestLibrary lib;
        QCOMPARE(code(lib.service->createArtist({QStringLiteral("A"), false, {}, {QStringLiteral("vaporwave")}})),
                 ErrorCode::UnknownTag);
        const ArtistId a = lib.artist(QStringLiteral("A"), false, {QStringLiteral("rock")});

        AlbumDraft d;
        d.primaryArtist = a;
        d.title = QStringLiteral("T");
        d.date = {2020, 0, 0};
        d.tags = {QStringLiteral("rock"), QStringLiteral("nonsense")};
        TrackInput t;
        t.draft = lib.draft(QStringLiteral("x"));
        d.tracks = {t};
        QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::UnknownTag);

        d.tags = {QStringLiteral("rock")};
        d.tracks[0].draft.tags = {QStringLiteral("nonsense")};
        QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::UnknownTag);
        QCOMPARE(lib.integrity(), QString());
    }

    void failedOperationRollsBackEverything()
    {
        TestLibrary lib;
        const ArtistId hidden = lib.artist(QStringLiteral("H"), true);
        AlbumDraft d;
        d.primaryArtist = hidden;
        d.title = QStringLiteral("T");
        d.date = {2020, 0, 0};
        TrackInput good;
        good.draft = lib.draft(QStringLiteral("ok"));
        TrackInput bad;
        bad.existing = TrackId(424242);  // does not exist -> fails in the middle of the transaction
        d.tracks = {good, bad};
        QCOMPARE(code(lib.service->createAlbum(d)), ErrorCode::NotFound);

        QVERIFY(lib.artistRow(hidden).hidden);  // not made visible
        TrackRepo tracks(*lib.db);
        QCOMPARE(tracks.count().value(), 0);
        QVERIFY(lib.storage.applied().isEmpty());
        // (no integrity check: a hidden artist created on its own is, by definition, not yet referenced)
    }

    // ================================================================ tags and visibility
    void artistsReceiveTagsFromTheirFirstAlbumOnce()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId co = lib.artist(QStringLiteral("Co"));
        const ArtistId untagged = lib.artist(QStringLiteral("U"));
        TagRepo tags(*lib.db);

        lib.album(a, QStringLiteral("One"), 1, {co}, {QStringLiteral("rock")});
        QCOMPARE(tags.tags(TagOwner::Artist, a.value).value(), QStringList{QStringLiteral("rock")});
        QCOMPARE(tags.tags(TagOwner::Artist, co.value).value(), QStringList{QStringLiteral("rock")});  // co-authors too

        lib.album(a, QStringLiteral("Two"), 1, {co}, {QStringLiteral("pop")});
        QCOMPARE(tags.tags(TagOwner::Artist, a.value).value(), QStringList{QStringLiteral("rock")});  // never again

        // An album without tags does not use up the one-time seeding.
        lib.album(untagged, QStringLiteral("Plain"), 1);
        QVERIFY(!lib.artistRow(untagged).tagsSeeded);
        lib.album(untagged, QStringLiteral("Tagged"), 1, {}, {QStringLiteral("jazz")});
        QCOMPARE(tags.tags(TagOwner::Artist, untagged.value).value(), QStringList{QStringLiteral("jazz")});
        QCOMPARE(lib.integrity(), QString());
    }

    void hiddenAlbumAuthorsBecomeVisible()
    {
        TestLibrary lib;
        const ArtistId hiddenPrimary = lib.artist(QStringLiteral("Hidden P"), true);
        const ArtistId hiddenCo = lib.artist(QStringLiteral("Hidden C"), true);
        QVERIFY(lib.artistRow(hiddenPrimary).dirName.isEmpty());

        lib.album(hiddenPrimary, QStringLiteral("X"), 1, {hiddenCo});
        QVERIFY(!lib.artistRow(hiddenPrimary).hidden);
        QCOMPARE(lib.artistRow(hiddenPrimary).dirName, QStringLiteral("Hidden P"));
        QVERIFY(!lib.artistRow(hiddenCo).hidden);
        QCOMPARE(lib.integrity(), QString());
    }

    void artistFoldersAreUniqueWhenNamesCollideAfterSanitising()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("AC/DC"));
        const ArtistId b = lib.artist(QStringLiteral("AC_DC"));  // sanitises to the same folder
        QCOMPARE(lib.artistRow(a).dirName, QStringLiteral("AC_DC"));
        QCOMPARE(lib.artistRow(b).dirName, QStringLiteral("AC_DC (2)"));
        QCOMPARE(lib.artistRow(a).name, QStringLiteral("AC/DC"));  // the database keeps the true name
    }

    // ================================================================ duplicates and memberships
    void sameSourceHashReusesTheExistingTrack()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId one = lib.album(a, QStringLiteral("One"), 2);
        const TrackId original = lib.trackAt(one, 1);
        const QByteArray hash = lib.track(original).contentHash;

        AlbumDraft d;
        d.primaryArtist = a;
        d.title = QStringLiteral("Two");
        d.date = {2021, 0, 0};
        TrackInput dup;
        dup.draft = lib.draft(QStringLiteral("whatever"));
        dup.draft.contentHash = hash;
        d.tracks = {dup};
        const auto two = lib.service->createAlbum(d);
        QVERIFY(two.ok());

        TrackRepo tracks(*lib.db);
        QCOMPARE(tracks.count().value(), 2);  // no new track row
        QCOMPARE(lib.trackAt(two.value(), 1), original);
        QCOMPARE(tracks.membershipCount(original).value(), 2);
        QCOMPARE(lib.track(original).home.id, one.value);  // still lives in the first album
        QCOMPARE(lib.integrity(), QString());
    }

    void removingFromHomeAlbumMovesTheFileToTheNextLocation()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId one = lib.album(a, QStringLiteral("One"), 3);
        const AlbumId two = lib.album(a, QStringLiteral("Two"), 1);
        const PlaylistId list = lib.playlistOfNew(QStringLiteral("Mix"), a, 1);
        const TrackId t = lib.trackAt(one, 2);

        lib.clock.advance(1000);
        QVERIFY(lib.service->addTrackToPlaylist(t, list).ok());  // earlier...
        lib.clock.advance(1000);
        QVERIFY(lib.service->addTrackToAlbum(t, two).ok());      // ...but albums are preferred
        lib.storage.clear();

        QVERIFY(lib.service->removeTrackFromAlbum(t, one).ok());
        const Track moved = lib.track(t);
        QCOMPARE(moved.home.kind, ContainerKind::Album);
        QCOMPARE(moved.home.id, two.value);
        QCOMPARE(moved.filePath, QStringLiteral("Artists/A/Two/02 - One track 2.mp3"));
        QVERIFY(lib.storage.applied().contains({FileOp::Kind::MoveFile, QStringLiteral("Artists/A/One/02 - One track 2.mp3"),
                                                QStringLiteral("Artists/A/Two/02 - One track 2.mp3")}));

        // Album One closed the gap: former track 3 is now number 2 and its file was renamed.
        const Track third = lib.track(lib.trackAt(one, 2));
        QCOMPARE(third.filePath, QStringLiteral("Artists/A/One/02 - One track 3.mp3"));
        QCOMPARE(lib.integrity(), QString());
    }

    void withOnlyPlaylistsLeftTheEarliestPlaylistWins()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId one = lib.album(a, QStringLiteral("One"), 2);
        const PlaylistId p1 = lib.playlistOfNew(QStringLiteral("P1"), a, 1);
        const PlaylistId p2 = lib.playlistOfNew(QStringLiteral("P2"), a, 1);
        const TrackId t = lib.trackAt(one, 1);
        lib.clock.advance(1000);
        QVERIFY(lib.service->addTrackToPlaylist(t, p2).ok());
        lib.clock.advance(1000);
        QVERIFY(lib.service->addTrackToPlaylist(t, p1).ok());

        QVERIFY(lib.service->removeTrackFromAlbum(t, one).ok());
        QCOMPARE(lib.track(t).home.id, p2.value);
        QCOMPARE(lib.track(t).filePath, QStringLiteral("Playlists/P2/One track 1 - A.mp3"));
        QCOMPARE(lib.integrity(), QString());
    }

    void removeFromContainerIsOnlyAllowedWhileTheTrackExistsElsewhere()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId one = lib.album(a, QStringLiteral("One"), 2);
        QCOMPARE(code(lib.service->removeTrackFromAlbum(lib.trackAt(one, 1), one)), ErrorCode::NotAllowed);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        QCOMPARE(code(lib.service->removeTrackFromPlaylist(lib.trackAt(p, 1), p)), ErrorCode::NotAllowed);
        QCOMPARE(lib.integrity(), QString());
    }

    void removingTheLastTrackThatLivesElsewhereDeletesTheEmptyContainer()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId one = lib.album(a, QStringLiteral("One"), 2);
        const PlaylistId list = lib.playlistOfNew(QStringLiteral("Mix"), a, 1);
        const TrackId shared = lib.trackAt(list, 1);
        QVERIFY(lib.service->addTrackToAlbum(shared, one).ok());
        // Mix now holds only `shared`, which also lives in album One.
        QVERIFY(lib.service->removeTrackFromAlbum(shared, one).ok());
        QVERIFY(lib.playlistExists(list));  // shared stays in its home playlist
        QVERIFY(lib.service->addTrackToAlbum(shared, one).ok());
        QVERIFY(lib.service->removeTrackFromPlaylist(shared, list).ok());
        QVERIFY(!lib.playlistExists(list));  // emptied playlist is deleted
        QCOMPARE(lib.track(shared).home.kind, ContainerKind::Album);
        QCOMPARE(lib.integrity(), QString());
    }

    void positionsStayContiguousAndFilesAreRenumbered()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("Four"), 4);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        const TrackId second = lib.trackAt(album, 2);
        QVERIFY(lib.service->addTrackToPlaylist(second, p).ok());
        QVERIFY(lib.service->removeTrackFromAlbum(second, album).ok());

        AlbumRepo albums(*lib.db);
        const QList<TrackId> ids = albums.tracks(album).value();
        QCOMPARE(ids.size(), 3);
        for (int i = 0; i < ids.size(); ++i) {
            QCOMPARE(albums.positionOf(album, ids[i]).value(), i + 1);
            QVERIFY2(lib.track(ids[i]).filePath.contains(QStringLiteral("/%1 - ").arg(i + 1, 2, 10, QLatin1Char('0'))),
                     qPrintable(lib.track(ids[i]).filePath));
        }
        QCOMPARE(lib.integrity(), QString());
    }

    // ================================================================ deleting (SPEC 6.4)
    void deletingTheOnlyTrackOfAnAlbumDeletesTheAlbum()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId single = lib.album(a, QStringLiteral("S"), 1);
        const TrackId t = lib.trackAt(single, 1);
        const auto report = lib.service->deleteTrack(t);
        QVERIFY(report.ok());
        QVERIFY(!lib.albumExists(single));
        QCOMPARE(report.value().albumsDeleted, QList<AlbumId>{single});
        QCOMPARE(report.value().tracksDeleted, QList<TrackId>{t});
        QVERIFY(lib.artistExists(a));  // an explicitly created artist stays, even without albums
        QVERIFY(!lib.artistRow(a).hidden);
        QCOMPARE(lib.integrity(), QString());
    }

    void deletingATrackRemovesItFromEveryContainer()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("Two"), 2);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        const TrackId t = lib.trackAt(album, 1);
        QVERIFY(lib.service->addTrackToPlaylist(t, p).ok());
        QVERIFY(lib.service->deleteTrack(t).ok());
        QVERIFY(!lib.trackExists(t));
        PlaylistRepo playlists(*lib.db);
        QCOMPARE(playlists.trackCount(p).value(), 1);
        QCOMPARE(lib.integrity(), QString());
    }

    void deleteAlbumKeepsTracksUsedElsewhere()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("Doomed"), 3);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("Keeper"), a, 1);
        const TrackId kept = lib.trackAt(album, 2);
        QVERIFY(lib.service->addTrackToPlaylist(kept, p).ok());
        lib.storage.clear();

        const auto plan = lib.service->planDeleteAlbum(album);
        QVERIFY(plan.ok());
        QVERIFY(lib.albumExists(album));  // a plan changes nothing
        QVERIFY(lib.storage.applied().isEmpty());

        const auto done = lib.service->deleteAlbum(album);
        QVERIFY(done.ok());
        QCOMPARE(plan.value(), done.value());  // the dialog text matches reality
        QCOMPARE(done.value().tracksKept, QList<TrackId>{kept});
        QCOMPARE(done.value().tracksDeleted.size(), 2);
        QVERIFY(!lib.albumExists(album));
        QVERIFY(lib.trackExists(kept));
        QCOMPARE(lib.track(kept).home.kind, ContainerKind::Playlist);
        QCOMPARE(lib.track(kept).filePath, QStringLiteral("Playlists/Keeper/Doomed track 2 - A.mp3"));
        QCOMPARE(lib.integrity(), QString());
    }

    void deletePlaylistDeletesOnlyTracksThatExistNowhereElse()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("Home"), 2);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("Temp"), a, 2);
        const TrackId inAlbum = lib.trackAt(album, 1);
        QVERIFY(lib.service->addTrackToPlaylist(inAlbum, p).ok());

        const auto plan = lib.service->planDeletePlaylist(p);
        const auto done = lib.service->deletePlaylist(p);
        QVERIFY(plan.ok() && done.ok());
        QCOMPARE(plan.value(), done.value());
        QCOMPARE(done.value().tracksDeleted.size(), 2);
        QCOMPARE(done.value().tracksKept, QList<TrackId>{inAlbum});
        QVERIFY(lib.trackExists(inAlbum));
        QVERIFY(lib.albumExists(album));
        QCOMPARE(lib.integrity(), QString());
    }

    void deletingAPlaylistTrackErasesUnreferencedHiddenArtists()
    {
        TestLibrary lib;
        const ArtistId hidden = lib.artist(QStringLiteral("Ghost"), true);
        const ArtistId other = lib.artist(QStringLiteral("Other"), true);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), hidden, 2);
        // second hidden artist credited on one of the tracks, too
        QVERIFY(TrackRepo(*lib.db).addCredit(lib.trackAt(p, 1), {other, 0, CreditRole::Feat}).ok());

        QVERIFY(lib.service->deleteTrack(lib.trackAt(p, 1)).ok());
        QVERIFY(lib.artistExists(hidden));   // still credited on the second track
        QVERIFY(!lib.artistExists(other));   // nothing references it any more: erased
        QVERIFY(lib.service->deletePlaylist(p).ok());
        QVERIFY(!lib.artistExists(hidden));
        QCOMPARE(lib.integrity(), QString());
    }

    // ================================================================ Unsorted tracks (SPEC 5.2)
    void hiddenArtistsUnsortedListIsVirtual()
    {
        TestLibrary lib;
        const ArtistId hidden = lib.artist(QStringLiteral("H"), true);
        const ArtistId main = lib.artist(QStringLiteral("M"));
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), hidden, 2);
        TrackInput featTrack;
        featTrack.draft = lib.draft(QStringLiteral("with feat"), {{main, CreditRole::Main}, {hidden, CreditRole::Feat}});
        lib.playlist(QStringLiteral("Q"), {featTrack});

        AlbumRepo albums(*lib.db);
        TrackRepo tracks(*lib.db);
        QVERIFY(!albums.unsortedOf(hidden).value().has_value());  // no row, no folder
        const QList<TrackId> list = tracks.unsortedOfHiddenArtist(hidden).value();
        QCOMPARE(list.size(), 2);  // the feat credit is not part of it
        QCOMPARE(lib.integrity(), QString());
    }

    void addToDiscographyCreatesTheRealUnsortedAlbum()
    {
        TestLibrary lib;
        const ArtistId hidden = lib.artist(QStringLiteral("H"), true);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), hidden, 2);
        const TrackId t = lib.trackAt(p, 1);
        lib.storage.clear();

        const auto unsorted = lib.service->addToDiscography(t, hidden);
        QVERIFY(unsorted.ok());
        QVERIFY(!lib.artistRow(hidden).hidden);
        QCOMPARE(lib.artistRow(hidden).dirName, QStringLiteral("H"));

        const Album album = *lib.albumRow(unsorted.value());
        QVERIFY(album.isUnsorted);
        QVERIFY(album.title.isEmpty());  // title is translated at display time, never stored
        QCOMPARE(album.type, AlbumType::Other);
        QCOMPARE(album.dirName, kUnsortedDirName);
        QCOMPARE(album.date, (ReleaseDate{2026, 10, 19}));  // creation date

        QCOMPARE(lib.track(t).home.id, unsorted.value().value);
        QCOMPARE(lib.track(t).filePath, QStringLiteral("Artists/H/Unsorted tracks/01 - P song 1.mp3"));
        PlaylistRepo playlists(*lib.db);
        QCOMPARE(playlists.trackCount(p).value(), 2);  // still in the playlist
        QVERIFY(lib.storage.applied().contains({FileOp::Kind::MoveFile, QStringLiteral("Playlists/P/P song 1 - H.mp3"),
                                                QStringLiteral("Artists/H/Unsorted tracks/01 - P song 1.mp3")}));

        // The hidden-artist query is still true for a visible artist, but the album is now real.
        QCOMPARE(lib.service->addToDiscography(t, hidden).value(), unsorted.value());  // idempotent
        QCOMPARE(lib.integrity(), QString());
    }

    void addToDiscographyCreditsTheChosenArtist()
    {
        TestLibrary lib;
        const ArtistId credited = lib.artist(QStringLiteral("C"), true);
        const ArtistId chosen = lib.artist(QStringLiteral("Chosen"), true);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), credited, 1);
        const TrackId t = lib.trackAt(p, 1);
        QVERIFY(lib.service->addToDiscography(t, chosen).ok());
        TrackRepo tracks(*lib.db);
        const QList<TrackCredit> credits = tracks.credits(t).value();
        QCOMPARE(credits.size(), 2);
        QCOMPARE(credits.last().artist, chosen);
        QCOMPARE(credits.last().role, CreditRole::Main);
        QCOMPARE(lib.integrity(), QString());
    }

    void materialiseHiddenArtistMovesAllOfTheirTracks()
    {
        TestLibrary lib;
        const ArtistId hidden = lib.artist(QStringLiteral("H"), true);
        const PlaylistId p1 = lib.playlistOfNew(QStringLiteral("P1"), hidden, 2);
        const PlaylistId p2 = lib.playlistOfNew(QStringLiteral("P2"), hidden, 1);
        Q_UNUSED(p1);
        Q_UNUSED(p2);
        const auto unsorted = lib.service->materialiseHiddenArtist(hidden);
        QVERIFY(unsorted.ok());
        AlbumRepo albums(*lib.db);
        QCOMPARE(albums.trackCount(unsorted.value()).value(), 3);
        QVERIFY(!lib.artistRow(hidden).hidden);
        QCOMPARE(lib.service->materialiseHiddenArtist(hidden).value(), unsorted.value());

        const ArtistId featOnly = lib.artist(QStringLiteral("F"), true);
        const ArtistId main = lib.artist(QStringLiteral("M"));
        TrackInput t;
        t.draft = lib.draft(QStringLiteral("x"), {{main, CreditRole::Main}, {featOnly, CreditRole::Feat}});
        lib.playlist(QStringLiteral("Q"), {t});
        QCOMPARE(code(lib.service->materialiseHiddenArtist(featOnly)), ErrorCode::NoTracks);
        QCOMPARE(lib.integrity(), QString());
    }

    void losingTheLastRealAlbumHidesTheArtistAndDissolvesUnsorted()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId real = lib.album(a, QStringLiteral("Only"), 2);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        const TrackId inPlaylist = lib.trackAt(p, 1);
        const auto unsorted = lib.service->addToDiscography(inPlaylist, a);
        QVERIFY(unsorted.ok());
        QCOMPARE(lib.track(inPlaylist).home.id, unsorted.value().value);

        const auto plan = lib.service->planDeleteAlbum(real);
        const auto done = lib.service->deleteAlbum(real);
        QVERIFY(plan.ok() && done.ok());
        QCOMPARE(plan.value(), done.value());
        QVERIFY(lib.artistRow(a).hidden);
        QVERIFY(lib.artistRow(a).dirName.isEmpty());
        QVERIFY(!lib.albumExists(unsorted.value()));  // real Unsorted album is gone...
        QVERIFY(lib.trackExists(inPlaylist));         // ...the track stays where the playlist needs it
        QCOMPARE(lib.track(inPlaylist).home.kind, ContainerKind::Playlist);
        QCOMPARE(lib.track(inPlaylist).filePath, QStringLiteral("Playlists/P/P song 1 - A.mp3"));
        QCOMPARE(done.value().artistsHidden, QList<ArtistId>{a});
        QCOMPARE(lib.integrity(), QString());

        // ...and the virtual list takes over.
        QCOMPARE(TrackRepo(*lib.db).unsortedOfHiddenArtist(a).value().size(), 1);
    }

    void artistWithoutRemainingCreditsStaysVisibleWhenItsLastAlbumGoes()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId only = lib.album(a, QStringLiteral("Only"), 2);
        QVERIFY(lib.service->deleteAlbum(only).ok());
        QVERIFY(lib.artistExists(a));
        QVERIFY(!lib.artistRow(a).hidden);
        QCOMPARE(lib.integrity(), QString());
    }

    // ================================================================ delete artist (UI doc p.33)
    void deletingAnArtistWithOnlySoloAlbumsErasesEverything()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId bystander = lib.artist(QStringLiteral("B"));
        lib.album(a, QStringLiteral("One"), 2);
        lib.album(a, QStringLiteral("Two"), 1);
        lib.album(bystander, QStringLiteral("Other"), 1);

        const auto plan = lib.service->planDeleteArtist(a);
        QVERIFY(plan.ok());
        QCOMPARE(plan.value().artistsDeleted, QList<ArtistId>{a});
        QVERIFY(plan.value().artistsHidden.isEmpty());
        QVERIFY(lib.artistExists(a));

        const auto done = lib.service->deleteArtist(a);
        QVERIFY(done.ok());
        QCOMPARE(plan.value(), done.value());
        QVERIFY(!lib.artistExists(a));
        QCOMPARE(done.value().albumsDeleted.size(), 2);
        QCOMPARE(done.value().tracksDeleted.size(), 3);
        QVERIFY(lib.artistExists(bystander));
        QCOMPARE(lib.integrity(), QString());
    }

    void deletingAnArtistStillNeededByAPlaylistKeepsThemHidden()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("One"), 2);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        const TrackId shared = lib.trackAt(album, 1);
        QVERIFY(lib.service->addTrackToPlaylist(shared, p).ok());

        const auto plan = lib.service->planDeleteArtist(a);
        const auto done = lib.service->deleteArtist(a);
        QVERIFY(plan.ok() && done.ok());
        QCOMPARE(plan.value(), done.value());
        QCOMPARE(done.value().artistsHidden, QList<ArtistId>{a});
        QVERIFY(lib.artistRow(a).hidden);
        QVERIFY(!lib.albumExists(album));
        QVERIFY(lib.trackExists(shared));
        QCOMPARE(lib.track(shared).home.kind, ContainerKind::Playlist);
        QCOMPARE(lib.integrity(), QString());
    }

    void deletingThePrimaryOfACollaborationRehomesTheAlbum()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId b = lib.artist(QStringLiteral("B"));
        const ArtistId c = lib.artist(QStringLiteral("C"));
        const AlbumId collab = lib.album(a, QStringLiteral("Together"), 2, {b, c});
        lib.storage.clear();

        const auto plan = lib.service->planDeleteArtist(a);
        const auto done = lib.service->deleteArtist(a);
        QVERIFY(plan.ok() && done.ok());
        QCOMPARE(plan.value(), done.value());
        QCOMPARE(done.value().albumsRehomed, QList<AlbumId>{collab});
        QCOMPARE(done.value().artistsHidden, QList<ArtistId>{a});

        const Album moved = *lib.albumRow(collab);
        QCOMPARE(moved.primaryArtist, b);  // lowest co-author position
        AlbumRepo albums(*lib.db);
        const QList<CoAuthor> co = albums.coAuthors(collab).value();
        QCOMPARE(co.size(), 2);
        QCOMPARE(co[0].artist, c);  // remaining co-author renumbered to 1
        QCOMPARE(co[0].position, 1);
        QCOMPARE(co[1].artist, a);  // the deleted artist stays credited, hidden
        QVERIFY(lib.artistRow(a).hidden);
        QCOMPARE(lib.track(lib.trackAt(collab, 1)).filePath, QStringLiteral("Artists/B/Together/01 - Together track 1.mp3"));
        QVERIFY(lib.storage.applied().contains({FileOp::Kind::MoveDir, QStringLiteral("Artists/A/Together"),
                                                QStringLiteral("Artists/B/Together")}));
        QCOMPARE(lib.integrity(), QString());
    }

    void rehomedAlbumGetsASuffixWhenTheNewOwnerHasThatTitle()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId b = lib.artist(QStringLiteral("B"));
        const AlbumId collab = lib.album(a, QStringLiteral("Hits"), 1, {b});
        lib.album(b, QStringLiteral("Hits"), 1);

        QVERIFY(lib.service->deleteArtist(a).ok());
        const Album moved = *lib.albumRow(collab);
        QCOMPARE(moved.primaryArtist, b);
        QCOMPARE(moved.title, QStringLiteral("Hits (2)"));
        QCOMPARE(moved.dirName, QStringLiteral("Hits (2)"));
        QCOMPARE(lib.integrity(), QString());
    }

    void deletingACoAuthorKeepsThemHiddenOnTheOthersAlbum()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId b = lib.artist(QStringLiteral("B"));
        const AlbumId bs = lib.album(b, QStringLiteral("B album"), 1, {a});
        const AlbumId own = lib.album(a, QStringLiteral("A album"), 1);
        const auto done = lib.service->deleteArtist(a);
        QVERIFY(done.ok());
        QVERIFY(!lib.albumExists(own));
        QVERIFY(lib.albumExists(bs));
        QVERIFY(lib.artistRow(a).hidden);  // still listed on B's album
        QCOMPARE(lib.integrity(), QString());
    }

    // ================================================================ plans, signals, recents
    void aPlanLeavesNoTraceAndSendsNoSignal()
    {
        TestLibrary lib;
        qRegisterMetaType<semplice::ChangeSet>();
        const ArtistId a = lib.artist(QStringLiteral("A"));
        lib.album(a, QStringLiteral("One"), 2);
        lib.storage.clear();
        QSignalSpy spy(lib.service.get(), &LibraryService::libraryChanged);

        QVERIFY(lib.service->planDeleteArtist(a).ok());
        QCOMPARE(spy.count(), 0);
        QVERIFY(lib.storage.applied().isEmpty());
        QVERIFY(lib.artistExists(a));
        QCOMPARE(lib.integrity(), QString());

        QVERIFY(lib.service->deleteArtist(a).ok());
        QCOMPARE(spy.count(), 1);
        const ChangeSet changes = spy.first().first().value<ChangeSet>();
        QVERIFY(changes.artists.contains(a));
        QVERIFY(!changes.albums.isEmpty());
        QVERIFY(!changes.tracks.isEmpty());
    }

    void deletingAnAlbumOrPlaylistDropsItFromRecentlyPlayed()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("One"), 1);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        RecentRepo recent(*lib.db);
        QVERIFY(recent.touch(ContainerKind::Album, album.value, 1).ok());
        QVERIFY(recent.touch(ContainerKind::Playlist, p.value, 2).ok());
        QVERIFY(lib.service->deleteAlbum(album).ok());
        QVERIFY(lib.service->deletePlaylist(p).ok());
        QVERIFY(recent.list().value().isEmpty());
        QCOMPARE(lib.integrity(), QString());
    }

    void fileOpsReachTheStorageOnlyAfterCommit()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("One"), 1);
        lib.storage.clear();
        QVERIFY(lib.service->deleteAlbum(album).ok());
        const QList<FileOp> ops = lib.storage.applied();
        QVERIFY(ops.contains({FileOp::Kind::Trash, QStringLiteral("Artists/A/One/01 - One track 1.mp3"), {}}));
        QVERIFY(ops.contains({FileOp::Kind::Trash, QStringLiteral("Artists/A/One"), {}}));
    }
};

QTEST_GUILESS_MAIN(TstLibraryRules)
#include "tst_library_rules.moc"
