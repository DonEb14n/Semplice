#include "PlaybackFakes.h"

#include "core/services/SettingsService.h"
#include "ui/BannerVm.h"
#include "ui/LibraryListModel.h"
#include "ui/LibraryVm.h"
#include "ui/NavigationVm.h"
#include "ui/PlayerVm.h"
#include "ui/QueueVm.h"
#include "ui/SettingsVm.h"
#include "ui/SearchVm.h"

#include <QSignalSpy>
#include <QtTest>

using namespace semplice;
using namespace semplice::testing;

namespace {
QList<ListRow> rows(int n, const QString& prefix = QStringLiteral("Item"))
{
    QList<ListRow> out;
    for (int i = 1; i <= n; ++i)
        out.append({i, QStringLiteral("%1 %2").arg(prefix).arg(i), QStringLiteral("Sub %1").arg(i), i % 2 ? QStringLiteral("Artists/A B/cover.jpg") : QString(), 100 + i, 2000 + i, 1});
    return out;
}
}  // namespace

// M6: the GUI-side view-models, without QML.
class TstUiModels : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<semplice::PlayState>("semplice::PlayState"); }

    // ================================================================ list model
    void theListModelExposesRolesAndCoverUrls()
    {
        LibraryListModel model;
        QSignalSpy counted(&model, &LibraryListModel::countChanged);
        model.setRows(rows(3));
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(counted.count(), 1);
        const QModelIndex i = model.index(0);
        QCOMPARE(model.data(i, LibraryListModel::TitleRole).toString(), QStringLiteral("Item 1"));
        QCOMPARE(model.data(i, LibraryListModel::OwnerRole).toLongLong(), 101);
        QCOMPARE(model.data(i, LibraryListModel::LetterRole).toString(), QStringLiteral("I"));
        QCOMPARE(model.data(i, LibraryListModel::CoverRole).toString(), QStringLiteral("image://cover/Artists/A%20B/cover.jpg"));
        QVERIFY(model.data(model.index(1), LibraryListModel::CoverRole).toString().isEmpty());
        QCOMPARE(model.idAt(2), 3);
        QCOMPARE(model.idAt(9), 0);
        QVERIFY(model.roleNames().values().contains("itemId"));
    }

    void aCarouselRowKnowsEveryArtistOfItsAlbum()
    {
        LibraryListModel model;
        ListRow row = rows(1).first();
        row.owners = {{7, QStringLiteral("Freddie Dredd")}, {9, QStringLiteral("JAK3")}};
        model.setRows({row, rows(2).last()});
        const QVariantList owners = model.ownersAt(0);
        QCOMPARE(owners.size(), 2);
        QCOMPARE(owners.at(1).toMap().value(QStringLiteral("id")).toLongLong(), 9);
        QCOMPARE(owners.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Freddie Dredd"));
        QVERIFY(model.ownersAt(1).isEmpty());  // a row without artist links falls back to its subtitle
        QVERIFY(model.ownersAt(5).isEmpty());
    }

    // ================================================================ settings: the theme
    void theThemeIsADraftUntilSavedAndAppliesWithoutARestart()
    {
        QTemporaryDir dir;
        SettingsService settings(dir.filePath(QStringLiteral("s.ini")));
        SettingsVm vm(settings);
        QCOMPARE(vm.themes().size(), 3);
        QCOMPARE(vm.theme(), QStringLiteral("true-white"));
        QVERIFY(!vm.darkApplied());
        vm.setTheme(QStringLiteral("dark"));
        QVERIFY(vm.dirty());
        QVERIFY(!vm.darkApplied());  // not saved yet: the program still looks as before
        vm.save();
        QVERIFY(!vm.dirty());
        QVERIFY(vm.darkApplied());
        QVERIFY(!vm.restartNeeded());  // only a new language needs a restart
        QCOMPARE(vm.themeName(), QStringLiteral("Dark"));
        vm.setTheme(QStringLiteral("true-white"));
        vm.reload();  // leaving the page without saving drops the draft
        QCOMPARE(vm.theme(), QStringLiteral("dark"));
    }

    // ================================================================ library view-model
    void tabsViewModesAndSortFollowTheRules()
    {
        QTemporaryDir dir;
        SettingsService settings(dir.filePath(QStringLiteral("s.ini")));
        UiState state(settings);
        {
            LibraryVm vm(state);
            QSignalSpy loads(&vm, &LibraryVm::loadRequested);
            QCOMPARE(vm.tab(), 1);  // artists by default
            QCOMPARE(vm.sortKey(), 1);  // recent
            QVERIFY(!vm.sortAscending());  // newest first
            vm.setReady(true);
            QCOMPARE(loads.count(), 1);
            QCOMPARE(loads.last().at(0).toInt(), 1);

            vm.chooseSort(0);  // alphabetical: starts with the down arrow = A to Z
            QVERIFY(vm.sortAscending());
            QVERIFY(vm.sortDown());
            QCOMPARE(loads.last().at(1).toInt(), 0);
            QCOMPARE(loads.last().at(2).toInt(), 0);
            vm.chooseSort(0);  // choosing the same key again changes nothing
            QVERIFY(vm.sortAscending());
            vm.setSortDown(false);  // the up arrow: Z to A
            QVERIFY(!vm.sortAscending());
            QVERIFY(!vm.sortDown());
            QCOMPARE(loads.last().at(2).toInt(), 1);
            vm.chooseSort(2);  // year does not exist for artists
            QCOMPARE(vm.sortKey(), 0);

            vm.setTab(2);
            QCOMPARE(vm.tab(), 2);
            QCOMPARE(vm.sortKey(), 1);  // the sort of every tab is its own
            QVERIFY(vm.keyAllowed(2));
            vm.chooseSort(2);
            QCOMPARE(vm.sortKey(), 2);
            vm.setViewMode(2);
            QCOMPARE(vm.viewMode(), 2);
            vm.setTab(1);
            QCOMPARE(vm.viewMode(), 0);  // view modes are per tab
        }
        // after a restart: tab and view mode come back, the sort does not
        LibraryVm again(state);
        QCOMPARE(again.tab(), 1);
        again.setTab(2);
        QCOMPARE(again.viewMode(), 2);
        QCOMPARE(again.sortKey(), 1);
    }

    void albumTypeFilterNeverHidesEverything()
    {
        QTemporaryDir dir;
        SettingsService settings(dir.filePath(QStringLiteral("s.ini")));
        UiState state(settings);
        LibraryVm vm(state);
        vm.setTab(2);
        vm.setReady(true);
        QSignalSpy loads(&vm, &LibraryVm::loadRequested);
        QCOMPARE(vm.typeMask(), LibraryVm::kAllTypes);
        vm.toggleType(0);  // hide singles
        QCOMPARE(loads.last().at(3).toInt(), LibraryVm::kAllTypes & ~1);
        for (int t = 1; t <= 5; ++t)
            vm.toggleType(t);  // hiding the last visible type shows everything again
        QCOMPARE(vm.typeMask(), LibraryVm::kAllTypes);
        QCOMPARE(loads.last().at(3).toInt(), 0);  // 0 = no filter
    }

    void rowsArriveForTheRightTabAndCloseClearsThem()
    {
        QTemporaryDir dir;
        SettingsService settings(dir.filePath(QStringLiteral("s.ini")));
        UiState state(settings);
        LibraryVm vm(state);
        vm.setReady(true);
        vm.setRows(1, rows(5));
        QCOMPARE(vm.count(), 5);
        vm.setRows(2, rows(2));
        QCOMPARE(vm.count(), 5);  // the albums list is not the visible one
        vm.setTab(2);
        QCOMPARE(vm.count(), 2);
        vm.setReady(false);
        QCOMPARE(vm.count(), 0);
    }

    // ================================================================ search
    void searchShowsFivePerCategoryAndShowMoreListsAll()
    {
        SearchVm search;
        QSignalSpy asked(&search, &SearchVm::queryRequested);
        search.setText(QStringLiteral("wild"));
        QVERIFY(asked.wait(1000));
        const int token = asked.last().at(1).toInt();

        SearchResults results;
        for (int i = 1; i <= 8; ++i)
            results.albums.top.append({SearchKind::Album, i, QStringLiteral("Album %1").arg(i), 0});
        results.albums.total = 8;
        results.artists.top.append({SearchKind::Artist, 1, QStringLiteral("Artist"), 0});
        results.artists.total = 1;
        search.setResults(results, token + 1);  // an answer for an older query is ignored
        QVERIFY(!search.hasResults());
        search.setResults(results, token);
        QVERIFY(search.hasResults());
        QCOMPARE(static_cast<LibraryListModel*>(search.albums())->count(), 5);
        QCOMPARE(search.total1(), 8);
        QVERIFY(search.canShowMore(SearchVm::Albums));
        QVERIFY(!search.canShowMore(SearchVm::Artists));  // five or fewer: no button
        QCOMPARE(search.total2(), 0);                     // empty categories are omitted by the view
        search.showMore(SearchVm::Albums);
        QCOMPARE(static_cast<LibraryListModel*>(search.albums())->count(), 8);
        QVERIFY(!search.canShowMore(SearchVm::Albums));
    }

    void clearingTheTextClearsTheResultsAndKeepsTheTextOtherwise()
    {
        SearchVm search;
        QSignalSpy asked(&search, &SearchVm::queryRequested);
        search.setText(QStringLiteral("abc"));
        QVERIFY(asked.wait(1000));
        SearchResults results;
        results.tracks.top.append({SearchKind::Track, 1, QStringLiteral("T"), 0});
        results.tracks.total = 1;
        search.setResults(results, asked.last().at(1).toInt());
        QVERIFY(search.hasResults());
        search.setText(QString());
        QVERIFY(!search.hasResults());
        QCOMPARE(search.text(), QString());
    }

    void typingFastAsksOnlyOnce()
    {
        SearchVm search;
        QSignalSpy asked(&search, &SearchVm::queryRequested);
        search.setText(QStringLiteral("a"));
        search.setText(QStringLiteral("ab"));
        search.setText(QStringLiteral("abc"));
        QVERIFY(asked.wait(1000));
        QTest::qWait(300);
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.first().at(0).toString(), QStringLiteral("abc"));
    }

    // ================================================================ banners + navigation
    void bannersAreCappedAtThreeAndTranslatedWithPlurals()
    {
        BannerVm banners;
        banners.info(QStringLiteral("tracks_skipped"), 1);
        banners.error(QStringLiteral("folder_not_empty"));
        banners.info(QStringLiteral("tracks_skipped"), 3);
        QCOMPARE(banners.rowCount(), 3);
        banners.error(QStringLiteral("something_else"));  // the generic "Error -1" banner
        QCOMPARE(banners.rowCount(), 3);
        QCOMPARE(banners.data(banners.index(2), BannerVm::TitleRole).toString(), QStringLiteral("Error -1"));
        QCOMPARE(banners.data(banners.index(2), BannerVm::TextRole).toString(), QStringLiteral("Something went wrong."));
        QVERIFY(banners.data(banners.index(2), BannerVm::IsErrorRole).toBool());
        const int id = banners.data(banners.index(0), BannerVm::IdRole).toInt();
        banners.dismiss(id);
        QCOMPARE(banners.rowCount(), 2);
    }

    void navigationPublishesThePageAndTheArrows()
    {
        NavigationVm nav;
        QSignalSpy changed(&nav, &NavigationVm::pageChanged);
        QVERIFY(!nav.canBack());
        nav.open(NavigationVm::Artist, 7);
        QCOMPARE(nav.kind(), static_cast<int>(NavigationVm::Artist));
        QCOMPARE(nav.pageId(), 7);
        QVERIFY(nav.canBack());
        nav.open(NavigationVm::Artist, 7);  // same page: nothing happens
        QCOMPARE(changed.count(), 1);
        nav.back();
        QCOMPARE(nav.kind(), static_cast<int>(NavigationVm::Home));
        QVERIFY(nav.canForward());
        nav.forward();
        QCOMPARE(nav.pageId(), 7);
        nav.home();
        QCOMPARE(nav.kind(), 0);
    }

    // ================================================================ player + queue view-models
    void thePlayerFormatsHeaderCoverAndTargets()
    {
        FakeAudioBackend backend;
        FakeCatalog catalog;
        FakeStore store;
        StepClock clock;
        FakeSettings settings;
        for (int i = 1; i <= 3; ++i)
            catalog.add(i);
        PlaybackService service({backend, catalog, store, clock, nullptr, &settings, 1});
        PlayerVm player;
        player.setVolumeSource(&settings);
        QueueVm queue;
        player.attach(&service);
        queue.attach(&service, &catalog);
        QVERIFY(!player.hasTrack());
        QVERIFY(!player.canNext());
        QCOMPARE(player.title(), QStringLiteral("Nothing is playing"));

        PlayRequest request;
        QueueGroup group;
        group.origin = {OriginKind::Discography, 9, QStringLiteral("The Band")};
        for (int i = 1; i <= 3; ++i)
            group.tracks.append({TrackId(i), 5, ContainerKind::Album});
        request.groups = {group};
        request.afterEnd = AfterEnd::Stop;
        QVERIFY(service.play(request));
        QVERIFY(player.hasTrack());
        QVERIFY(player.playing());
        QCOMPARE(player.headerText(), QStringLiteral("The Band"));
        QVERIFY(player.headerIsDiscography());
        QCOMPARE(player.headerKind(), static_cast<int>(NavigationVm::Artist));
        QCOMPARE(player.headerId(), 9);
        QCOMPARE(player.title(), QStringLiteral("Track 1"));
        QVERIFY(player.coverUrl().isEmpty());  // no cover anywhere: the placeholder
        QVERIFY(player.canNext());
        QVERIFY(!player.canPrevious());

        // queue view-model: current, next; the popup lists the whole group
        QVERIFY(queue.hasCurrent());
        QVERIFY(!queue.hasPrevious());
        QCOMPARE(queue.nextTitle(), QStringLiteral("Track 2"));
        QCOMPARE(queue.count(), 3);
        QCOMPARE(queue.currentRow(), 0);
        queue.playRow(2);
        QCOMPARE(player.title(), QStringLiteral("Track 3"));
        QCOMPARE(queue.previousTitle(), QStringLiteral("Track 1"));
        QVERIFY(queue.data(queue.index(2), QueueVm::IsCurrentRole).toBool());
        queue.playPrevious();  // no 5-second rule when clicking the previous track
        QCOMPARE(player.title(), QStringLiteral("Track 1"));

        // volume
        player.setVolume(0.8);
        QCOMPARE(settings.m_volume, 0.8);
        player.toggleMute();
        QVERIFY(player.muted());
        player.setVolume(0.3);  // moving the slider while muted un-mutes
        QVERIFY(!player.muted());
        player.cycleRepeat();
        QCOMPARE(player.repeat(), 1);
        player.cycleRepeat();
        QCOMPARE(player.repeat(), 2);
        player.cycleRepeat();
        QCOMPARE(player.repeat(), 0);
        player.toggleShuffle();
        QVERIFY(player.shuffle());

        player.attach(nullptr);
        QVERIFY(!player.hasTrack());
        QCOMPARE(player.volume(), 0.3);  // without a library the settings still hold the level
    }
};

QTEST_GUILESS_MAIN(TstUiModels)
#include "tst_ui_models.moc"
