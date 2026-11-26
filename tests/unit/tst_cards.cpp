#include "TestLibrary.h"

#include "core/cards/CardQueries.h"

using namespace semplice;
using namespace semplice::testing;

class TstCards : public QObject {
    Q_OBJECT

    static int sectionTotal(const ArtistCard& card, ArtistSection s)
    {
        for (const auto& sec : card.sections)
            if (sec.section == s)
                return sec.total;
        return 0;
    }

private slots:
    void anArtistCardCountsTypesAndShowsOnlyNonEmptyCarousels()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("Band"), false, {QStringLiteral("rock")});
        const ArtistId other = lib.artist(QStringLiteral("Other"));
        const AlbumId album = lib.album(a, QStringLiteral("Full"), 8, {}, {}, {2010, 1, 1});
        const AlbumId single = lib.album(a, QStringLiteral("Hit"), 1, {}, {}, {2012, 1, 1});
        AlbumPatch toSingle;
        toSingle.type = AlbumType::Single;
        QVERIFY(lib.service->updateAlbum(single, toSingle).ok());
        const AlbumId collab = lib.album(other, QStringLiteral("Together"), 2, {a}, {}, {2015, 1, 1});
        Q_UNUSED(album)
        Q_UNUSED(collab)

        CardQueries q(*lib.db);
        const ArtistCard card = q.artist(a).value();
        QCOMPARE(card.artist.name, QStringLiteral("Band"));
        QCOMPARE(card.albums, 2);  // the collaboration counts as an authored album
        QCOMPARE(card.singles, 1);
        QCOMPARE(card.eps, 0);
        QCOMPARE(card.tags, QStringList{QStringLiteral("rock")});
        QCOMPARE(sectionTotal(card, ArtistSection::Discography), 3);  // authored incl. the collaboration
        QCOMPARE(sectionTotal(card, ArtistSection::Albums), 2);
        QCOMPARE(sectionTotal(card, ArtistSection::Singles), 1);
        QCOMPARE(sectionTotal(card, ArtistSection::Collaborations), 1);
        QCOMPARE(sectionTotal(card, ArtistSection::Eps), 0);
        QCOMPARE(sectionTotal(card, ArtistSection::Feats), 0);
        for (const auto& s : card.sections)
            QVERIFY(!s.items.isEmpty());  // empty carousels are not shown
        // the discography is newest first
        const auto disco = q.section(a, ArtistSection::Discography, 0).value();
        QCOMPARE(disco.first().title, QStringLiteral("Together"));
        QCOMPARE(disco.last().title, QStringLiteral("Full"));
    }

    void featsAreAlbumsWhereTheArtistOnlyAppearsOnTracks()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("Guest"));
        const ArtistId host = lib.artist(QStringLiteral("Host"));
        const AlbumId album = lib.album(host, QStringLiteral("Their Album"), 2);
        TrackPatch patch;
        patch.credits = QList<CreditDraft>{CreditDraft(host), CreditDraft(a, CreditRole::Feat)};
        QVERIFY(lib.service->updateTrack(lib.trackAt(album, 1), patch).ok());
        CardQueries q(*lib.db);
        const ArtistCard card = q.artist(a).value();
        QCOMPARE(sectionTotal(card, ArtistSection::Feats), 1);
        QCOMPARE(sectionTotal(card, ArtistSection::Discography), 0);  // not an author of anything
        QCOMPARE(q.section(a, ArtistSection::Feats, 0).value().first().title, QStringLiteral("Their Album"));
    }

    void aHiddenArtistHasOnlyUnsortedTracksAndFeats()
    {
        TestLibrary lib;
        const ArtistId visible = lib.artist(QStringLiteral("V"));
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("Mix"), visible, 2);
        TrackPatch patch;
        patch.credits = QList<CreditDraft>{CreditDraft(QStringLiteral("Ghost"))};
        QVERIFY(lib.service->updateTrack(lib.trackAt(p, 1), patch).ok());
        ArtistRepo artists(*lib.db);
        const ArtistId ghost = artists.findByName(QStringLiteral("Ghost")).value()->id;

        CardQueries q(*lib.db);
        const ArtistCard card = q.artist(ghost).value();
        QVERIFY(card.artist.hidden);
        QCOMPARE(card.sections.size(), 1);
        QCOMPARE(card.sections.first().section, ArtistSection::Discography);
        QCOMPARE(card.sections.first().items.first().id, -ghost.value);  // the virtual album

        const AlbumCard virtualAlbum = q.album(-ghost.value).value();
        QVERIFY(virtualAlbum.isUnsorted);
        QCOMPARE(virtualAlbum.tracks.size(), 1);
        QCOMPARE(virtualAlbum.artists.first().name, QStringLiteral("Ghost"));
        QVERIFY(!q.album(-visible.value).ok());  // only hidden artists have one
    }

    void anAlbumCardHasArtistsTagsTracksAndMenuFacts()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId b = lib.artist(QStringLiteral("B"));
        const AlbumId album = lib.album(a, QStringLiteral("Both"), 3, {b}, {QStringLiteral("jazz")}, {2001, 5, 17});
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        QVERIFY(lib.service->addTrackToPlaylist(lib.trackAt(album, 2), p).ok());

        CardQueries q(*lib.db);
        const AlbumCard card = q.album(album.value).value();
        QCOMPARE(card.title, QStringLiteral("Both"));
        QCOMPARE(card.date, (ReleaseDate{2001, 5, 17}));
        QCOMPARE(card.artists.size(), 2);
        QCOMPARE(card.artists.at(0).name, QStringLiteral("A"));
        QCOMPARE(card.artists.at(1).name, QStringLiteral("B"));
        QCOMPARE(card.tags, QStringList{QStringLiteral("jazz")});
        QCOMPARE(card.tracks.size(), 3);
        QCOMPARE(card.tracks.at(0).position, 1);
        QCOMPARE(card.tracks.at(0).memberships, 1);  // may not be removed: it exists nowhere else
        QCOMPARE(card.tracks.at(1).memberships, 2);  // also in a playlist: Remove is allowed
        QVERIFY(card.tracks.at(1).inPlaylist);
        QCOMPARE(card.tracks.at(1).albumTitle, QStringLiteral("Both"));
        QCOMPARE(card.durationMs, 3 * 180000);

        const PlaylistCard pc = q.playlist(p).value();
        QCOMPARE(pc.name, QStringLiteral("P"));
        QCOMPARE(pc.tracks.size(), 2);
        QCOMPARE(pc.tracks.at(1).albumTitle, QStringLiteral("Both"));  // the "Album name" column
        QVERIFY(pc.tracks.at(0).albumTitle.isEmpty());
        QVERIFY(!q.album(9999).ok());
        QVERIFY(!q.playlist(PlaylistId(9999)).ok());
    }

    void sectionKeysRoundTrip()
    {
        const QString key = CardQueries::sectionKey(ArtistId(7), ArtistSection::Eps);
        const auto parsed = CardQueries::parseSectionKey(key);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->first, ArtistId(7));
        QCOMPARE(parsed->second, ArtistSection::Eps);
        QVERIFY(!CardQueries::parseSectionKey(QStringLiteral("section:0:eps")).has_value());
        QVERIFY(!CardQueries::parseSectionKey(QStringLiteral("year:2000")).has_value());
    }

    void artistPickerSearchesAllArtistsIncludingHidden()
    {
        TestLibrary lib;
        lib.artist(QStringLiteral("Arti"));
        lib.artist(QStringLiteral("Art of music"));
        lib.artist(QStringLiteral("Zed"));
        lib.artist(QStringLiteral("Artful Ghost"), /*hidden=*/true);
        CardQueries q(*lib.db);
        QCOMPARE(q.artists(QStringLiteral("art"), 10).value().size(), 3);
        QCOMPARE(q.artists(QStringLiteral("art"), 10, /*includeHidden=*/false).value().size(), 2);
        QCOMPARE(q.artists({}, 2).value().size(), 2);  // limited
        QCOMPARE(q.artists(QStringLiteral("ART"), 10).value().first().name, QStringLiteral("Art of music"));  // sorted by name
    }
};

QTEST_GUILESS_MAIN(TstCards)
#include "tst_cards.moc"
