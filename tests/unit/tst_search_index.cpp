#include "TestLibrary.h"

#include "core/services/SearchIndex.h"

#include <QtTest>

using namespace semplice;
using namespace semplice::testing;

class TstSearchIndex : public QObject {
    Q_OBJECT
private:
    static QStringList names(const SearchCategory& c)
    {
        QStringList out;
        for (const SearchHit& h : c.top)
            out << h.display;
        return out;
    }

private slots:
    void ranksPrefixBeforeWordStartBeforeSubstring()
    {
        TestLibrary lib;
        for (const char* n : {"Abba", "Gabba Gabba", "The Abbacus", "Cabbage", "Zzz"})
            lib.artist(QString::fromLatin1(n));
        SearchIndex index;
        QVERIFY(index.rebuild(*lib.db).ok());

        const SearchResults r = index.search(QStringLiteral("abba"));
        QCOMPARE(names(r.artists), (QStringList{"Abba", "The Abbacus", "Cabbage", "Gabba Gabba"}));
        QCOMPARE(r.artists.top[0].rank, 0);
        QCOMPARE(r.artists.top[1].rank, 1);  // starts a word
        QCOMPARE(r.artists.top[2].rank, 2);  // only inside a word
        QCOMPARE(r.artists.total, 4);
    }

    void limitsPerCategoryButCountsEverything()
    {
        TestLibrary lib;
        for (int i = 0; i < 12; ++i)
            lib.artist(QStringLiteral("Band %1").arg(i, 2, 10, QLatin1Char('0')));
        SearchIndex index;
        QVERIFY(index.rebuild(*lib.db).ok());
        const SearchResults r = index.search(QStringLiteral("band"), 5);
        QCOMPARE(r.artists.top.size(), 5);
        QCOMPARE(r.artists.total, 12);  // "Show more" appears because total > 5
        QVERIFY(r.albums.top.isEmpty());
    }

    void ignoresDiacriticsCaseAndWorksForCyrillicAndCjk()
    {
        TestLibrary lib;
        lib.artist(QStringLiteral("Beyoncé"));
        lib.artist(QStringLiteral("Виктор Цой"));          // Виктор Цой
        lib.artist(QStringLiteral("椅名林檎"));                                          // 椎名林檎
        SearchIndex index;
        QVERIFY(index.rebuild(*lib.db).ok());

        QCOMPARE(index.search(QStringLiteral("BEYONCE")).artists.total, 1);
        QCOMPARE(index.search(QStringLiteral("цой")).artists.total, 1);  // цой
        QCOMPARE(index.search(QStringLiteral("林檎")).artists.total, 1);        // substring in the middle
        QCOMPARE(index.search(QStringLiteral("   ")).artists.total, 0);                 // blank query
    }

    void hiddenArtistsAreOnlyFoundWhenAsked()
    {
        TestLibrary lib;
        lib.artist(QStringLiteral("Ghost"), true);
        lib.artist(QStringLiteral("Ghostbusters"));
        SearchIndex index;
        QVERIFY(index.rebuild(*lib.db).ok());
        QCOMPARE(index.search(QStringLiteral("ghost")).artists.total, 1);
        QCOMPARE(index.searchArtists(QStringLiteral("ghost"), 10, true).total, 2);
    }

    void unsortedAlbumsAreNotIndexed()
    {
        TestLibrary lib;
        const ArtistId h = lib.artist(QStringLiteral("H"), true);
        const PlaylistId p = lib.playlistOfNew(QStringLiteral("P"), h, 1);
        QVERIFY(lib.service->addToDiscography(lib.trackAt(p, 1), h).ok());
        SearchIndex index;
        QVERIFY(index.rebuild(*lib.db).ok());
        QCOMPARE(index.size(SearchKind::Album), 0);
        QCOMPARE(index.size(SearchKind::Playlist), 1);
        QCOMPARE(index.size(SearchKind::Track), 1);
    }

    void followsLibraryChangesIncrementally()
    {
        TestLibrary lib;
        SearchIndex index;
        QVERIFY(index.rebuild(*lib.db).ok());
        QObject::connect(lib.service.get(), &LibraryService::libraryChanged, [&](const ChangeSet& c) {
            QVERIFY(index.refresh(*lib.db, c).ok());
        });

        const ArtistId a = lib.artist(QStringLiteral("Queen"));
        const AlbumId album = lib.album(a, QStringLiteral("Jazz"), 2);
        QCOMPARE(index.search(QStringLiteral("queen")).artists.total, 1);
        QCOMPARE(index.search(QStringLiteral("jazz")).albums.total, 1);
        QCOMPARE(index.search(QStringLiteral("jazz track")).tracks.total, 2);

        QVERIFY(lib.service->deleteAlbum(album).ok());
        QCOMPARE(index.search(QStringLiteral("jazz")).albums.total, 0);
        QCOMPARE(index.search(QStringLiteral("jazz track")).tracks.total, 0);

        QVERIFY(lib.service->deleteArtist(a).ok());
        QCOMPARE(index.search(QStringLiteral("queen")).artists.total, 0);

        // Incremental state equals a full rebuild.
        SearchIndex fresh;
        QVERIFY(fresh.rebuild(*lib.db).ok());
        for (SearchKind k : {SearchKind::Artist, SearchKind::Album, SearchKind::Playlist, SearchKind::Track})
            QCOMPARE(index.size(k), fresh.size(k));
    }
};

QTEST_GUILESS_MAIN(TstSearchIndex)
#include "tst_search_index.moc"
