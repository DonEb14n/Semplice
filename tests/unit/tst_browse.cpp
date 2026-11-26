#include "TestLibrary.h"

#include "core/browse/BannerQueue.h"
#include "core/browse/LibraryQueries.h"
#include "core/browse/NavigationHistory.h"
#include "core/browse/PlayRequestBuilder.h"
#include "core/browse/UiState.h"
#include "core/services/SettingsService.h"

using namespace semplice;
using namespace semplice::testing;

class TstBrowse : public QObject {
    Q_OBJECT

    static QStringList names(const QList<ArtistRow>& rows)
    {
        QStringList out;
        for (const auto& r : rows)
            out << r.name;
        return out;
    }
    static QStringList titles(const QList<AlbumRow>& rows)
    {
        QStringList out;
        for (const auto& r : rows)
            out << r.title;
        return out;
    }

private slots:
    // ================================================================ library lists
    void artistsAreSortedAlphabeticallyAndHiddenOnesAreLeftOut()
    {
        TestLibrary lib;
        lib.artist(QStringLiteral("Beta"));
        lib.artist(QStringLiteral("alpha"));
        lib.artist(QStringLiteral("Гамма"));
        lib.artist(QStringLiteral("Secret"), /*hidden=*/true);
        LibraryQueries q(*lib.db);
        QCOMPARE(names(q.artists({SortKey::Alphabet, SortDirection::Ascending}).value()),
                 (QStringList{QStringLiteral("alpha"), QStringLiteral("Beta"), QStringLiteral("Гамма")}));
        QCOMPARE(names(q.artists({SortKey::Alphabet, SortDirection::Descending}).value()),
                 (QStringList{QStringLiteral("Гамма"), QStringLiteral("Beta"), QStringLiteral("alpha")}));
    }

    void anArtistHiddenByHandIsListedOnlyUnderTheHiddenFilter()
    {
        TestLibrary lib;
        const ArtistId beta = lib.artist(QStringLiteral("Beta"));
        lib.artist(QStringLiteral("Alpha"));
        QVERIFY(ArtistRepo(*lib.db).setUserHidden(beta, true, 1).ok());
        LibraryQueries q(*lib.db);
        const LibrarySort sort{SortKey::Alphabet, SortDirection::Ascending};
        QCOMPARE(names(q.artists(sort).value()), (QStringList{QStringLiteral("Alpha")}));
        const auto hidden = q.artists(sort, true).value();
        QCOMPARE(names(hidden), (QStringList{QStringLiteral("Beta")}));
        QVERIFY(hidden.first().userHidden);
    }

    void recentMeansNewestFirstByDefault()
    {
        TestLibrary lib;
        lib.artist(QStringLiteral("First"));
        lib.clock.advance(1000);
        lib.artist(QStringLiteral("Second"));
        lib.clock.advance(1000);
        lib.artist(QStringLiteral("Third"));
        LibraryQueries q(*lib.db);
        QCOMPARE(names(q.artists({SortKey::Recent, SortDirection::Descending}).value()),
                 (QStringList{QStringLiteral("Third"), QStringLiteral("Second"), QStringLiteral("First")}));
        QCOMPARE(names(q.artists({SortKey::Recent, SortDirection::Ascending}).value()).first(), QStringLiteral("First"));
    }

    void albumsSortByYearAndFilterByType()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const ArtistId b = lib.artist(QStringLiteral("B"));
        lib.album(a, QStringLiteral("Old"), 8, {}, {}, {1999, 1, 1});
        lib.album(a, QStringLiteral("Mid"), 2, {}, {}, {2005, 3, 1});   // single (1-3 tracks)? type comes from the draft: Album
        lib.album(b, QStringLiteral("New"), 1, {a}, {}, {2021, 6, 1});
        LibraryQueries q(*lib.db);
        QCOMPARE(titles(q.albums({SortKey::Year, SortDirection::Descending}).value()),
                 (QStringList{QStringLiteral("New"), QStringLiteral("Mid"), QStringLiteral("Old")}));
        QCOMPARE(titles(q.albums({SortKey::Year, SortDirection::Ascending}).value()).first(), QStringLiteral("Old"));
        QCOMPARE(titles(q.albums({SortKey::Alphabet, SortDirection::Ascending}).value()),
                 (QStringList{QStringLiteral("Mid"), QStringLiteral("New"), QStringLiteral("Old")}));
        // the artists text lists the primary first, then the co-authors
        const auto rows = q.albums({SortKey::Year, SortDirection::Descending}).value();
        QCOMPARE(rows.first().artists, QStringLiteral("B, A"));
        QCOMPARE(rows.first().year, 2021);
        // a type that no album has: nothing; all types: everything
        QCOMPARE(q.albums({}, {AlbumType::Live}).value().size(), 0);
        QCOMPARE(q.albums({}, {AlbumType::Album}).value().size(), 3);
        QCOMPARE(q.albums({}).value().size(), 3);
    }

    void theUnsortedAlbumIsNeverListed()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        const AlbumId album = lib.album(a, QStringLiteral("One"), 1);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), a, 1);
        QVERIFY(lib.service->addToDiscography(lib.trackAt(p, 1), a).ok());
        LibraryQueries q(*lib.db);
        QCOMPARE(q.albums({}).value().size(), 1);
        QCOMPARE(q.albums({}).value().first().id, album);
    }

    void playlistsAndCoverPaths()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("A"));
        lib.playlistOfNew(QStringLiteral("Zeta"), a, 1);
        lib.clock.advance(1000);
        lib.playlistOfNew(QStringLiteral("alpha"), a, 1);
        LibraryQueries q(*lib.db);
        const auto byName = q.playlists({SortKey::Alphabet, SortDirection::Ascending}).value();
        QCOMPARE(byName.first().name, QStringLiteral("alpha"));
        const auto recent = q.playlists({SortKey::Recent, SortDirection::Descending}).value();
        QCOMPARE(recent.first().name, QStringLiteral("alpha"));
        QVERIFY(LibraryQueries::isAllowed(LibraryTab::Albums, SortKey::Year));
        QVERIFY(!LibraryQueries::isAllowed(LibraryTab::Artists, SortKey::Year));
    }

    // ================================================================ play requests
    void discographyIsNewestFirstWithEveryEntryTaggedByAlbum()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("Band"));
        const ArtistId other = lib.artist(QStringLiteral("Other"));
        const AlbumId oldAlbum = lib.album(a, QStringLiteral("Old"), 2, {}, {}, {1990, 1, 1});
        const AlbumId newAlbum = lib.album(a, QStringLiteral("New"), 3, {}, {}, {2020, 1, 1});
        const AlbumId collab = lib.album(other, QStringLiteral("Collab"), 1, {a}, {}, {2010, 1, 1});
        lib.album(other, QStringLiteral("NotMine"), 1, {}, {}, {2015, 1, 1});

        PlayRequestBuilder builder(*lib.db);
        const PlayRequest r = builder.discography(a).value();
        QCOMPARE(r.afterEnd, AfterEnd::Recommend);
        QCOMPARE(r.groups.size(), 1);
        const QueueGroup& g = r.groups.first();
        QCOMPARE(g.origin.kind, OriginKind::Discography);
        QCOMPARE(g.origin.title, QStringLiteral("Band"));
        QCOMPARE(g.tracks.size(), 6);
        QCOMPARE(g.tracks.at(0).sourceId, newAlbum.value);
        QCOMPARE(g.tracks.at(2).sourceId, newAlbum.value);
        QCOMPARE(g.tracks.at(3).sourceId, collab.value);
        QCOMPARE(g.tracks.at(4).sourceId, oldAlbum.value);
        QCOMPARE(g.tracks.at(3).track, lib.trackAt(collab, 1));
    }

    void albumPlaylistAndSingleTrackRequests()
    {
        TestLibrary lib;
        const ArtistId a = lib.artist(QStringLiteral("Band"));
        const AlbumId album = lib.album(a, QStringLiteral("Rec"), 3);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("Mix"), a, 2);
        PlayRequestBuilder builder(*lib.db);

        const PlayRequest ar = builder.album(album).value();
        QCOMPARE(ar.afterEnd, AfterEnd::Recommend);
        QCOMPARE(ar.groups.first().origin.kind, OriginKind::Album);
        QCOMPARE(ar.groups.first().tracks.size(), 3);

        const PlayRequest pr = builder.playlist(p).value();
        QCOMPARE(pr.afterEnd, AfterEnd::Stop);
        QCOMPARE(pr.groups.first().origin.title, QStringLiteral("Mix"));
        QCOMPARE(pr.groups.first().tracks.size(), 2);

        const PlayRequest sr = builder.singleTrack(lib.trackAt(album, 2)).value();
        QCOMPARE(sr.afterEnd, AfterEnd::Stop);  // search never starts the endless stream
        QCOMPARE(sr.groups.first().tracks.size(), 1);
        QCOMPARE(sr.groups.first().origin.title, QStringLiteral("Rec"));

        QVERIFY(!builder.album(AlbumId(999)).ok());
        QVERIFY(!builder.discography(ArtistId(999)).ok());
    }

    void aHiddenArtistPlaysTheirPlaylistTracks()
    {
        TestLibrary lib;
        const ArtistId visible = lib.artist(QStringLiteral("V"));
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("Mix"), visible, 1);
        (void)lib.service->addTrackToPlaylist(lib.trackAt(p, 1), p);  // no-op (already a member)
        TrackPatch patch;
        patch.credits = QList<CreditDraft>{CreditDraft(QStringLiteral("Ghost"))};
        QVERIFY(lib.service->updateTrack(lib.trackAt(p, 1), patch).ok());
        ArtistRepo artists(*lib.db);
        const ArtistId ghost = artists.findByName(QStringLiteral("Ghost")).value()->id;
        QVERIFY(artists.get(ghost).value()->hidden);
        const PlayRequest r = PlayRequestBuilder(*lib.db).hiddenArtist(ghost).value();
        QCOMPARE(r.groups.first().tracks.size(), 1);
        QCOMPARE(r.groups.first().origin.title, QStringLiteral("Ghost"));
    }

    // ================================================================ history
    void backAndForwardWalkTheHistory()
    {
        NavigationHistory h;
        QVERIFY(!h.canBack());
        QVERIFY(!h.canForward());
        QVERIFY(h.open({PageKind::Artist, 1, {}}));
        QVERIFY(h.open({PageKind::Album, 2, {}}));
        QVERIFY(!h.open({PageKind::Album, 2, {}}));  // same page: nothing happens
        QVERIFY(h.canBack());
        QCOMPARE(h.back()->kind, PageKind::Artist);
        QVERIFY(h.canForward());
        QCOMPARE(h.forward()->kind, PageKind::Album);
        QVERIFY(!h.forward());
        h.back();
        h.back();
        QCOMPARE(h.current().kind, PageKind::Home);
        QVERIFY(!h.back());
    }

    void openingFromThePastDropsTheForwardPart()
    {
        NavigationHistory h;
        h.open({PageKind::Artist, 1, {}});
        h.open({PageKind::Album, 2, {}});
        h.back();
        h.open({PageKind::Playlist, 3, {}});
        QVERIFY(!h.canForward());
        QCOMPARE(h.back()->kind, PageKind::Artist);
    }

    void deletedPagesAreForgotten()
    {
        NavigationHistory h;
        h.open({PageKind::Artist, 1, {}});
        h.open({PageKind::Album, 2, {}});
        h.open({PageKind::Artist, 1, {}});
        h.forget(PageKind::Album, 2);
        QCOMPARE(h.current().kind, PageKind::Artist);
        QVERIFY(h.canBack());
        QCOMPARE(h.back()->kind, PageKind::Home);  // the two artist entries merged into one
    }

    void historyHasAUpperLimit()
    {
        NavigationHistory h;
        for (int i = 1; i <= 250; ++i)
            h.open({PageKind::Album, i, {}});
        int steps = 0;
        while (h.back())
            ++steps;
        QCOMPARE(steps, NavigationHistory::kLimit - 1);
    }

    // ================================================================ banners
    void atMostThreeBannersTheOldestGoesFirst()
    {
        BannerQueue q;
        const int first = q.push(BannerKind::Info, QStringLiteral("a"));
        q.push(BannerKind::Error, QStringLiteral("b"));
        q.push(BannerKind::Info, QStringLiteral("c"), 3);
        QCOMPARE(q.banners().size(), 3);
        q.push(BannerKind::Info, QStringLiteral("d"));
        QCOMPARE(q.banners().size(), 3);
        QCOMPARE(q.banners().first().key, QStringLiteral("b"));
        QCOMPARE(q.banners().last().key, QStringLiteral("d"));  // the newest is last (shown below)
        QVERIFY(!q.dismiss(first));
        QVERIFY(q.dismiss(q.banners().first().id));
        QCOMPARE(q.banners().size(), 2);
        q.clear();
        QVERIFY(q.banners().isEmpty());
    }

    // ================================================================ ui state
    void tabAndViewModesAreRememberedPerTab()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("s.ini"));
        {
            SettingsService s(ini);
            UiState state(s);
            QCOMPARE(state.tab(), LibraryTab::Artists);
            QCOMPARE(state.viewMode(LibraryTab::Albums), ViewMode::List);
            state.setTab(LibraryTab::Albums);
            state.setViewMode(LibraryTab::Albums, ViewMode::ExtraLarge);
            state.setViewMode(LibraryTab::Artists, ViewMode::Large);
        }
        SettingsService s(ini);
        UiState state(s);
        QCOMPARE(state.tab(), LibraryTab::Albums);
        QCOMPARE(state.viewMode(LibraryTab::Albums), ViewMode::ExtraLarge);
        QCOMPARE(state.viewMode(LibraryTab::Artists), ViewMode::Large);
        QCOMPARE(state.viewMode(LibraryTab::Playlists), ViewMode::List);
    }
};

QTEST_GUILESS_MAIN(TstBrowse)
#include "tst_browse.moc"
