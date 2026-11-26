// The editor drafts: what can be saved (SPEC 8.3, UI doc p.36-41), what a scan fills in, what the command holds.
#include "core/tags/GenreVocabulary.h"
#include "ui/EditorVm.h"

#include <QSignalSpy>
#include <QtTest>

using namespace semplice;

namespace {
ScanSummary albumScan()
{
    ScanSummary s;
    s.hasProposal = true;
    s.kind = 0;
    s.albumTitle = QStringLiteral("Proposed");
    s.authors = {QStringLiteral("Band")};
    s.albumType = 2;
    s.year = 2019;
    for (int i = 0; i < 2; ++i) {
        ScanRow r;
        r.index = i;
        r.title = QStringLiteral("Song %1").arg(i + 1);
        r.creditNames = {QStringLiteral("Band")};
        r.creditRoles = {0};
        r.durationMs = 61000;
        s.rows.append(r);
    }
    return s;
}
}  // namespace

class TstEditorVm : public QObject {
    Q_OBJECT
    GenreVocabulary m_genres;

private slots:
    void initTestCase()
    {
        auto genres = GenreVocabulary::load();
        QVERIFY(genres.ok());
        m_genres = std::move(genres).value();
    }

    void anAlbumNeedsTitleArtistValidDateAndTracks()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.setCurrentYear(2026);
        ed.beginAdd(QStringLiteral("album"));
        QVERIFY(!ed.canSave());
        ed.setTitle(QStringLiteral("Record"));
        ed.addAuthor(0, QStringLiteral("Someone"));
        ed.setDate(2020, 0, 0);
        QVERIFY(!ed.canSave());  // no tracks yet
        ed.applyScan(albumScan());
        QVERIFY(ed.canSave());

        ed.setDate(2020, 0, 15);  // a day without a month
        QVERIFY(!ed.dateValid());
        QVERIFY(!ed.canSave());
        ed.setDate(1743, 1, 1);  // before the first possible year
        QVERIFY(!ed.dateValid());
        ed.setDate(2027, 1, 1);  // next year is still allowed
        QVERIFY(ed.dateValid());
        ed.setDate(2028, 1, 1);
        QVERIFY(!ed.dateValid());
        ed.setDate(2021, 2, 30);  // February has no 30th
        QVERIFY(!ed.dateValid());
        ed.setDate(2020, 2, 29);
        QVERIFY(ed.canSave());

        ed.removeAuthor(0);
        ed.removeAuthor(0);  // the scan added its own proposed artist too
        QVERIFY(!ed.canSave());
    }

    void anArtistKnownToTheLibraryComesWithItsIdAndPicture()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginAdd(QStringLiteral("album"));
        ScanSummary scan = albumScan();
        scan.authorRefs = {ScanAuthor{7, QStringLiteral("Band"), QStringLiteral("Artists/Band/artist_avatar.jpg"), false}};
        ed.applyScan(scan);
        const auto rows = qobject_cast<CreditModel*>(ed.authors())->rows();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().id, qint64(7));            // so its tags can be copied and its picture shown
        QVERIFY(!rows.first().avatar.isEmpty());
    }

    void artistsOfTheNewFilesCanBePickedByNameAndTheirTagsAreAskedForByName()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginAdd(QStringLiteral("album"));
        ScanSummary scan = albumScan();
        scan.rows.first().creditNames = {QStringLiteral("Band"), QStringLiteral("Guest")};
        scan.rows.first().creditRoles = {0, 1};
        ed.applyScan(scan);                               // the author is "Band"
        const QVariantList pending = ed.pendingArtists();
        QCOMPARE(pending.size(), 1);
        QCOMPARE(pending.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Guest"));
        ed.addAuthor(0, QStringLiteral("Guest"));
        QVERIFY(ed.pendingArtists().isEmpty());

        QSignalSpy asked(&ed, &EditorVm::artistTagsRequested);
        ed.copyTagsFromArtists();                         // authors without an id are sent by name
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.first().first().toList().size(), 2);
    }

    void theFirstScanFillsTheFormAndOnlyOnce()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginAdd(QStringLiteral("album"));
        ed.applyScan(albumScan());
        QCOMPARE(ed.title(), QStringLiteral("Proposed"));
        QCOMPARE(ed.albumType(), 2);
        QCOMPARE(ed.year(), 2019);
        QCOMPARE(qobject_cast<CreditModel*>(ed.authors())->count(), 1);
        QCOMPARE(qobject_cast<EditRowModel*>(ed.rows())->count(), 2);

        ed.setTitle(QStringLiteral("Mine"));
        ScanSummary more = albumScan();
        more.albumTitle = QStringLiteral("Other");
        more.rows.first().index = 2;
        more.rows.last().index = 3;
        ed.applyScan(more);  // more files: rows are added, the form is not overwritten
        QCOMPARE(ed.title(), QStringLiteral("Mine"));
        QCOMPARE(qobject_cast<EditRowModel*>(ed.rows())->count(), 4);
    }

    void aPlaylistLookingDropOnAddAlbumBecomesAPlaylist()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginAdd(QStringLiteral("album"));
        ScanSummary s = albumScan();
        s.kind = 1;
        s.playlistName = QStringLiteral("Road trip");
        ed.applyScan(s);
        QCOMPARE(ed.entity(), QStringLiteral("playlist"));
        QCOMPARE(ed.title(), QStringLiteral("Road trip"));
        QVERIFY(ed.canSave());
    }

    void aTrackNeedsTitleAndAnArtist()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginEdit(QStringLiteral("track"), 5);
        QVERIFY(!ed.canSave());  // still loading
        TrackEdit t;
        t.id = 5;
        t.title = QStringLiteral("Old");
        t.artistNames = {QStringLiteral("A")};
        t.artistIds = {1};
        t.roles = {0};
        t.hidden = {false};
        ed.fillFromTrack(t);
        QVERIFY(ed.canSave());
        ed.setTitle(QStringLiteral("  "));
        QVERIFY(!ed.canSave());
        ed.setTitle(QStringLiteral("New"));
        ed.removeAuthor(0);
        QVERIFY(!ed.canSave());
    }

    void tagsComeOnlyFromTheVocabularyAndAreNotRepeated()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginAdd(QStringLiteral("artist"));
        const QVariantList choices = ed.tagChoices();
        QVERIFY(!choices.isEmpty());
        const QString key = choices.first().toMap().value(QStringLiteral("id")).toString();
        ed.addTag(key);
        ed.addTag(key);
        QCOMPARE(ed.tags().size(), 1);
        QVERIFY(ed.tagChoices().size() == choices.size() - 1);
        ed.removeTag(0);
        QCOMPARE(ed.tags().size(), 0);
    }

    void saveSendsOneCommandWithTheDraft()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.setCurrentYear(2026);
        ed.beginAdd(QStringLiteral("album"));
        ed.applyScan(albumScan());
        QSignalSpy spy(&ed, &EditorVm::saveRequested);
        ed.save();
        QCOMPARE(spy.count(), 1);
        const QVariantMap c = spy.value(0).value(0).toMap();
        QCOMPARE(c.value(QStringLiteral("type")).toString(), QStringLiteral("album"));
        QCOMPARE(c.value(QStringLiteral("mode")).toString(), QStringLiteral("add"));
        QCOMPARE(c.value(QStringLiteral("title")).toString(), QStringLiteral("Proposed"));
        QCOMPARE(c.value(QStringLiteral("rows")).toList().size(), 2);
        QVERIFY(ed.busy());
        ed.save();  // a second click while busy does nothing
        QCOMPARE(spy.count(), 1);
        ed.saveFinished(false);
        QVERIFY(!ed.busy());
        QVERIFY(ed.canSave());
    }

    void aTrackCommandNeverCarriesTagsAndARefreshKeepsTheDraft()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginEdit(QStringLiteral("track"), 5);
        TrackEdit t;
        t.id = 5;
        t.title = QStringLiteral("Old");
        t.artistNames = {QStringLiteral("A")};
        t.artistIds = {1};
        t.roles = {0};
        t.hidden = {false};
        t.tags = {QStringLiteral("rock")};
        ed.fillFromTrack(t);
        QSignalSpy spy(&ed, &EditorVm::saveRequested);
        ed.save();
        QCOMPARE(spy.count(), 1);
        QVERIFY(!spy.value(0).value(0).toMap().contains(QStringLiteral("tags")));  // a track keeps its albums' tags

        EditorVm album(&m_genres, QStringLiteral("en"));
        album.beginEdit(QStringLiteral("album"), 9);
        AlbumCard card;
        card.id = 9;
        card.title = QStringLiteral("From the database");
        album.fillFromAlbum(card);  // the window is loaded
        QSignalSpy load(&album, &EditorVm::loadRequested);
        album.refreshRows();
        QCOMPARE(load.count(), 1);
        album.setTitle(QStringLiteral("My draft"));
        // a refresh asked for while the window is still loading is ignored (it would swallow the first full answer)
        EditorVm loading(&m_genres, QStringLiteral("en"));
        loading.beginEdit(QStringLiteral("album"), 9);
        QSignalSpy none(&loading, &EditorVm::loadRequested);
        loading.refreshRows();
        QCOMPARE(none.count(), 0);
        TrackRow row;
        row.id = 3;
        row.title = QStringLiteral("T");
        row.memberships = 2;
        card.tracks = {row};
        album.fillFromAlbum(card);
        QCOMPARE(album.title(), QStringLiteral("My draft"));  // only the rows were refreshed
        auto* rows = qobject_cast<EditRowModel*>(album.rows());
        QCOMPARE(rows->count(), 1);
        QVERIFY(rows->rows().first().canRemove);
    }

    void aRefreshLeftOverFromTheLastWindowDoesNotEmptyTheNextOne()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginEdit(QStringLiteral("album"), 9);
        AlbumCard card;
        card.id = 9;
        card.title = QStringLiteral("Real title");
        ed.fillFromAlbum(card);
        ed.refreshRows();  // a library change asked for the rows, the user leaves before the answer comes ...
        ed.clear();
        ed.beginEdit(QStringLiteral("album"), 9);  // ... and opens the same album again
        ed.fillFromAlbum(card);
        QCOMPARE(ed.title(), QStringLiteral("Real title"));  // it used to stay empty
    }

    void aDuplicateArtistChipIsRefused()
    {
        EditorVm ed(&m_genres, QStringLiteral("en"));
        ed.beginAdd(QStringLiteral("album"));
        ed.addAuthor(3, QStringLiteral("Band"));
        ed.addAuthor(3, QStringLiteral("Band"));
        ed.addAuthor(0, QStringLiteral("band"));
        QCOMPARE(qobject_cast<CreditModel*>(ed.authors())->count(), 1);
    }
};

QTEST_MAIN(TstEditorVm)
#include "tst_editor_vm.moc"
