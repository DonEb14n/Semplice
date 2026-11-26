#include "ui/HomeVm.h"

#include <QSignalSpy>
#include <QtTest>

using namespace semplice;

namespace {

CarouselItem album(qint64 id, const QString& title, const QString& artists = QStringLiteral("Band"), int year = 2000)
{
    CarouselItem i;
    i.kind = ContainerKind::Album;
    i.id = id;
    i.title = title;
    i.subtitle = artists;
    i.year = year;
    i.ownerId = 7;
    i.albumType = 1;
    return i;
}
CarouselItem playlist(qint64 id, const QString& name)
{
    CarouselItem i;
    i.kind = ContainerKind::Playlist;
    i.id = id;
    i.title = name;
    return i;
}
CategoryContents contents(const QString& key, const QList<CarouselItem>& items, qint64 durationMs = 3600000)
{
    CategoryContents c;
    c.spec = *CategorySpec::fromKey(key);
    c.items = items;
    c.durationMs = durationMs;
    c.requestedKey = key;
    return c;
}

}  // namespace

// M7: the Home and category view-models without QML.
class TstHomeVm : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<QList<semplice::ContainerRef>>();
    }

    void titlesOfTheCategories()
    {
        auto genres = GenreVocabulary::load();
        QVERIFY(genres.ok());
        const GenreVocabulary& g = genres.value();
        QCOMPARE(categoryTitle({CategoryType::Year, QStringLiteral("2019"), {}}, &g, QStringLiteral("en")), QStringLiteral("2019"));
        QCOMPARE(categoryTitle({CategoryType::Decade, QStringLiteral("1990"), {}}, &g, QStringLiteral("en")), QStringLiteral("1990s"));
        QCOMPARE(categoryTitle({CategoryType::ArtistDiscography, QStringLiteral("5"), QStringLiteral("ABBA")}, &g, QStringLiteral("en")),
                 QStringLiteral("ABBA: Discography"));
        QCOMPARE(categoryTitle({CategoryType::Genre, QStringLiteral("rock"), {}}, &g, QStringLiteral("en")), g.name(QStringLiteral("rock"), QStringLiteral("en")));
        QCOMPARE(categoryTitle({CategoryType::YourAlbums, {}, {}}, &g, QStringLiteral("en")), QStringLiteral("Your Albums"));
        QCOMPARE(categoryTitle({CategoryType::YourPlaylists, {}, {}}, &g, QStringLiteral("en")), QStringLiteral("Your Playlists"));
    }

    void rowsKeepAlbumTypesAndMarkPlaylists()
    {
        const QList<ListRow> rows = toListRows({album(1, QStringLiteral("A")), playlist(2, QStringLiteral("P"))});
        QCOMPARE(rows.at(0).type, 1);
        QCOMPARE(rows.at(1).type, -1);
        const QList<ContainerRef> refs = toRefs(rows);
        QCOMPARE(refs.at(0), (ContainerRef{ContainerKind::Album, 1}));
        QCOMPARE(refs.at(1), (ContainerRef{ContainerKind::Playlist, 2}));
    }

    void theGreetingFollowsTheLibraryAndTheName()
    {
        QString name = QStringLiteral("Listener");
        HomeVm vm([&name] { return name; }, nullptr, QStringLiteral("en"));
        QVERIFY(vm.greeting().contains(QStringLiteral("Welcome, Listener!")));  // nothing loaded: empty library
        HomeData data;
        data.hasContent = true;
        vm.setHome(data);
        QVERIFY(vm.greeting().contains(QStringLiteral("Listener")));
        QVERIFY(!vm.greeting().startsWith(QStringLiteral("Welcome,")));  // an occasion / time-of-day greeting
        name = QStringLiteral("Anna");
        vm.refreshGreeting();
        QVERIFY(vm.greeting().contains(QStringLiteral("Anna")));
    }

    void homeFillsRecentAndCategoriesAndKeepsThemOnRecentOnlyUpdates()
    {
        HomeVm vm([] { return QStringLiteral("X"); }, nullptr, QStringLiteral("en"));
        QSignalSpy asked(&vm, &HomeVm::homeRequested);
        vm.refresh(false);
        QCOMPARE(asked.count(), 1);
        QVERIFY(asked.last().at(0).toStringList().isEmpty());  // nothing known yet: the engine generates five

        HomeData data;
        data.hasContent = true;
        data.recent = {album(1, QStringLiteral("R1")), playlist(2, QStringLiteral("R2"))};
        data.categories = {contents(QStringLiteral("year:2001"), {album(3, QStringLiteral("Y1")), album(4, QStringLiteral("Y2"))}),
                           contents(QStringLiteral("playlists"), {playlist(2, QStringLiteral("P"))})};
        vm.setHome(data);
        QVERIFY(vm.loaded());
        QCOMPARE(static_cast<LibraryListModel*>(vm.recent())->count(), 2);
        auto* categories = static_cast<CategoryListModel*>(vm.categories());
        QCOMPARE(categories->rowCount(), 2);
        QCOMPARE(categories->data(categories->index(0), CategoryListModel::KeyRole).toString(), QStringLiteral("year:2001"));
        QCOMPARE(categories->data(categories->index(0), CategoryListModel::TitleRole).toString(), QStringLiteral("2001"));
        auto* items = qobject_cast<LibraryListModel*>(categories->data(categories->index(0), CategoryListModel::ItemsRole).value<QObject*>());
        QVERIFY(items);
        QCOMPARE(items->count(), 2);

        // refreshing later asks again for the SAME categories
        vm.refresh(false);
        QCOMPARE(asked.last().at(0).toStringList(), (QStringList{QStringLiteral("year:2001"), QStringLiteral("playlists")}));

        // something was played: only Recently Played changes
        HomeData recentOnly;
        recentOnly.recentOnly = true;
        recentOnly.hasContent = true;
        recentOnly.recent = {playlist(9, QStringLiteral("New"))};
        vm.setHome(recentOnly);
        QCOMPARE(static_cast<LibraryListModel*>(vm.recent())->count(), 1);
        QCOMPARE(categories->rowCount(), 2);
    }

    void moreCategoriesAreRequestedOnceUntilTheyArriveAndStopWhenContentIsUsedUp()
    {
        HomeVm vm([] { return QStringLiteral("X"); }, nullptr, QStringLiteral("en"));
        QSignalSpy more(&vm, &HomeVm::moreRequested);
        vm.loadMore();
        QCOMPARE(more.count(), 0);  // not loaded yet
        HomeData data;
        data.hasContent = true;
        vm.setHome(data);
        vm.loadMore();
        vm.loadMore();
        QCOMPARE(more.count(), 1);  // one request in flight
        vm.addCategories({contents(QStringLiteral("year:2005"), {album(1, QStringLiteral("A"))})});
        auto* categories = static_cast<CategoryListModel*>(vm.categories());
        QCOMPARE(categories->rowCount(), 1);
        vm.loadMore();
        QCOMPARE(more.count(), 2);
        vm.addCategories({contents(QStringLiteral("year:2005"), {album(1, QStringLiteral("A"))})});  // a duplicate is ignored
        QCOMPARE(categories->rowCount(), 1);
        vm.loadMore();
        QCOMPARE(more.count(), 3);
        vm.addCategories({});  // nothing left
        vm.loadMore();
        QCOMPARE(more.count(), 3);
        vm.setHome(data);  // a full refresh starts the search for more categories again
        vm.loadMore();
        QCOMPARE(more.count(), 4);
    }

    void playingFromACarouselSendsAllItemsAndTheClickedIndex()
    {
        HomeVm vm([] { return QStringLiteral("X"); }, nullptr, QStringLiteral("en"));
        QSignalSpy play(&vm, &HomeVm::carouselPlayRequested);
        HomeData data;
        data.hasContent = true;
        data.recent = {album(1, QStringLiteral("A")), playlist(2, QStringLiteral("P")), album(3, QStringLiteral("B"))};
        vm.setHome(data);
        vm.playFromCarousel(vm.recent(), 2);
        QCOMPARE(play.count(), 1);
        const auto refs = play.last().at(0).value<QList<ContainerRef>>();
        QCOMPARE(refs.size(), 3);
        QCOMPARE(refs.at(1).kind, ContainerKind::Playlist);
        QCOMPARE(play.last().at(1).toInt(), 2);
        vm.playFromCarousel(vm.recent(), 9);  // out of range: ignored
        QCOMPARE(play.count(), 1);
    }

    void theCategoryPageShowsTitleStatsAndPlays()
    {
        CategoryVm vm(nullptr, QStringLiteral("en"));
        QSignalSpy asked(&vm, &CategoryVm::categoryRequested);
        QSignalSpy all(&vm, &CategoryVm::playAllRequested);
        QSignalSpy one(&vm, &CategoryVm::carouselPlayRequested);
        vm.open(QStringLiteral("year:2001"));
        QCOMPARE(asked.count(), 1);
        QVERIFY(!vm.ready());

        // an answer for another page is ignored
        vm.setContents(contents(QStringLiteral("decade:1990"), {album(1, QStringLiteral("Old"))}));
        QVERIFY(!vm.ready());

        vm.setContents(contents(QStringLiteral("year:2001"), {album(1, QStringLiteral("A")), album(2, QStringLiteral("B")), album(3, QStringLiteral("C"))}, 6 * 3600000 + 20 * 60000));
        QVERIFY(vm.ready());
        QCOMPARE(vm.title(), QStringLiteral("2001"));
        QVERIFY2(vm.stats().contains(QStringLiteral("6 h 20 min")), qPrintable(vm.stats()));
        QCOMPARE(static_cast<LibraryListModel*>(vm.items())->count(), 3);

        vm.playAll();
        QCOMPARE(all.count(), 1);
        QCOMPARE(all.last().at(1).toString(), QStringLiteral("2001"));
        QCOMPARE(all.last().at(2).toString(), QStringLiteral("year:2001"));
        vm.playItem(1);
        QCOMPARE(one.last().at(1).toInt(), 1);
        vm.playItem(5);
        QCOMPARE(one.count(), 1);

        vm.open(QStringLiteral("year:2001"));  // already open: no new request
        QCOMPARE(asked.count(), 1);
    }

    void recentlyPlayedIsOpenedByItsOwnKey()
    {
        CategoryVm vm(nullptr, QStringLiteral("en"));
        vm.open(QStringLiteral("recent"));
        CategoryContents c;
        c.requestedKey = QStringLiteral("recent");
        c.items = {album(1, QStringLiteral("A")), playlist(2, QStringLiteral("P"))};
        c.durationMs = 90 * 60000;
        vm.setContents(c);
        QVERIFY(vm.ready());
        QCOMPARE(vm.title(), QStringLiteral("Recently Played"));
        QVERIFY2(vm.stats().contains(QStringLiteral("1 h 30 min")), qPrintable(vm.stats()));
        QVERIFY2(vm.stats().startsWith(QStringLiteral("2")), qPrintable(vm.stats()));  // mixed items: "2 item(s)"
    }

    void statisticsTextVariants()
    {
        QVERIFY(CategoryVm::statsText(toListRows({playlist(1, QStringLiteral("P"))}), 5 * 60000).contains(QStringLiteral("5 min")));
        QVERIFY(CategoryVm::statsText(toListRows({album(1, QStringLiteral("A"))}), 0).contains(QStringLiteral("0 min")));
    }
};

QTEST_GUILESS_MAIN(TstHomeVm)
#include "tst_home_vm.moc"
