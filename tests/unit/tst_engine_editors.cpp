// The editor paths of the engine thread's object, called directly (same thread): scan -> save -> card -> edit -> delete plan.
// No real delete is executed here (the Recycle Bin is not touched): plans are rolled back by the service.
#include "TestAudioFiles.h"

#include "ui/AsyncPlaybackStore.h"
#include "ui/DeleteText.h"
#include "ui/LibraryEngine.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace semplice;
using namespace semplice::testing;

class TstEngineEditors : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QTemporaryDir m_music;
    LibraryEngine* m_engine = nullptr;
    int m_token = 100;

    OperationResult save(const QVariantMap& command)
    {
        QSignalSpy spy(m_engine, &LibraryEngine::operationFinished);
        m_engine->saveEntity(++m_token, command);
        return spy.value(0).value(0).value<OperationResult>();
    }
    ScanSummary scan(const QStringList& paths, bool reset = true)
    {
        QSignalSpy spy(m_engine, &LibraryEngine::scanReady);
        m_engine->scanFiles(++m_token, paths, 0, reset);
        return spy.value(0).value(0).value<ScanSummary>();
    }
    static QVariantList rowsOf(const ScanSummary& s)
    {
        QVariantList rows;
        for (const ScanRow& r : s.rows)
            rows.append(QVariantMap{{"index", r.index}, {"title", r.title}, {"names", r.creditNames}, {"roles", QVariantList{0}}});
        return rows;
    }
    QStringList makeFiles(const QString& prefix, int count, int seedStart)
    {
        QStringList out;
        for (int i = 0; i < count; ++i)
            out << writeTinyAudio(m_music.path(), QStringLiteral("%1 %2").arg(prefix).arg(i + 1), AudioFormat::Mp3, seedStart + i);
        return out;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<semplice::OperationResult>();
        qRegisterMetaType<semplice::ScanSummary>();
        qRegisterMetaType<semplice::AlbumCard>();
        qRegisterMetaType<semplice::ArtistCard>();
        qRegisterMetaType<semplice::PlaylistCard>();
        qRegisterMetaType<semplice::DeleteSummary>();
        qRegisterMetaType<semplice::CategoryContents>();
        qRegisterMetaType<QList<semplice::ListRow>>();
        qRegisterMetaType<semplice::TrackEdit>();
        QVERIFY(m_dir.isValid());
        QVERIFY(m_music.isValid());
        m_engine = new LibraryEngine;
        QSignalSpy opened(m_engine, &LibraryEngine::opened);
        QVERIFY(QDir().mkpath(m_dir.path() + QStringLiteral("/library")));
        m_engine->selectFolder(m_dir.path() + QStringLiteral("/library"));
        QCOMPARE(opened.count(), 1);
    }
    void cleanupTestCase() { delete m_engine; }

    void anArtistIsCreatedAndAHiddenOneIsKept()
    {
        QVariantMap cmd{{"type", "artist"}, {"mode", "add"}, {"name", "Solo Act"}, {"tags", QStringList{}}, {"hidden", false}};
        const OperationResult made = save(cmd);
        QCOMPARE(made.code, 0);
        QVERIFY(made.id > 0);
        QCOMPARE(made.entity, QStringLiteral("artist"));
        // the same name again: refused with "already exists"
        const OperationResult again = save(cmd);
        QCOMPARE(again.code, static_cast<int>(ErrorCode::AlreadyExists));
    }

    void anAlbumFromScannedFilesRoundTripsThroughTheCard()
    {
        const ScanSummary s = scan(makeFiles(QStringLiteral("Song"), 3, 10));
        QCOMPARE(s.rows.size(), 3);
        QVariantMap cmd{{"type", "album"}, {"mode", "add"}, {"title", "Round Trip"}, {"albumType", 1}, {"year", 2020}, {"month", 5}, {"day", 0},
                        {"tags", QStringList{}}, {"authors", QVariantList{QVariantMap{{"id", 0}, {"name", "Tester"}}}}, {"rows", rowsOf(s)}, {"startOver", true}};
        const OperationResult r = save(cmd);
        QCOMPARE(r.code, 0);
        QVERIFY(r.id > 0);

        QSignalSpy card(m_engine, &LibraryEngine::albumCardReady);
        m_engine->loadAlbumCard(r.id);
        QCOMPARE(card.count(), 1);
        const AlbumCard c = card.value(0).value(0).value<AlbumCard>();
        QCOMPARE(c.title, QStringLiteral("Round Trip"));
        QCOMPARE(c.tracks.size(), 3);
        QCOMPARE(c.date.year, 2020);
        QCOMPARE(c.artists.size(), 1);

        // edit: new title, same tracks
        QVariantMap edit{{"type", "album"}, {"mode", "edit"}, {"id", r.id}, {"title", "Renamed"}, {"albumType", 1}, {"year", 2021},
                         {"authors", QVariantList{QVariantMap{{"id", c.artists.at(0).id}, {"name", "Tester"}}}}, {"tags", QStringList{}}};
        QCOMPARE(save(edit).code, 0);
        QSignalSpy card2(m_engine, &LibraryEngine::albumCardReady);
        m_engine->loadAlbumCard(r.id);
        const AlbumCard c2 = card2.value(0).value(0).value<AlbumCard>();
        QCOMPARE(c2.title, QStringLiteral("Renamed"));
        QCOMPARE(c2.date.year, 2021);

        // a delete plan states the real consequence and changes nothing
        QSignalSpy plan(m_engine, &LibraryEngine::deletePlanReady);
        m_engine->planDelete(static_cast<int>(OperationKind::DeleteAlbum), r.id);
        QCOMPARE(plan.count(), 1);
        const DeleteSummary d = plan.value(0).value(0).value<DeleteSummary>();
        QVERIFY(d.ok);
        QCOMPARE(d.tracksDeleted, 3);
        QVERIFY(!deleteText(d).isEmpty());
        QSignalSpy still(m_engine, &LibraryEngine::albumCardReady);
        m_engine->loadAlbumCard(r.id);
        QCOMPARE(still.value(0).value(0).value<AlbumCard>().tracks.size(), 3);

        // a track edit
        const qint64 trackId = c2.tracks.at(0).id;
        QSignalSpy te(m_engine, &LibraryEngine::trackEditReady);
        m_engine->loadTrackEdit(trackId);
        const TrackEdit t = te.value(0).value(0).value<TrackEdit>();
        QCOMPARE(t.artistNames.size(), 1);
        QVariantMap tcmd{{"type", "track"}, {"id", trackId}, {"title", "Better Title"}, {"tags", QStringList{}},
                         {"credits", QVariantList{QVariantMap{{"id", t.artistIds.at(0)}, {"name", t.artistNames.at(0)}, {"role", 0}}}}};
        QCOMPARE(save(tcmd).code, 0);
        QSignalSpy te2(m_engine, &LibraryEngine::trackEditReady);
        m_engine->loadTrackEdit(trackId);
        QCOMPARE(te2.value(0).value(0).value<TrackEdit>().title, QStringLiteral("Better Title"));
        // the album is still there after a track was edited (its page must stay open)
        QSignalSpy afterEdit(m_engine, &LibraryEngine::albumCardReady);
        QSignalSpy failed(m_engine, &LibraryEngine::cardFailed);
        m_engine->loadAlbumCard(r.id);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(afterEdit.count(), 1);
        // the same with a brand new artist credited on the track
        QVariantMap other{{"type", "track"}, {"id", trackId}, {"title", "Better Title"},
                          {"credits", QVariantList{QVariantMap{{"id", t.artistIds.at(0)}, {"name", t.artistNames.at(0)}, {"role", 0}},
                                                   QVariantMap{{"id", 0}, {"name", "Brand New Guest"}, {"role", 0}}}}};
        QCOMPARE(save(other).code, 0);
        QSignalSpy again(m_engine, &LibraryEngine::albumCardReady);
        m_engine->loadAlbumCard(r.id);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(again.count(), 1);

        // a playlist from an existing track, then the same name is refused
        QVariantMap pl{{"type", "playlist"}, {"mode", "add"}, {"name", "Mix"}, {"existing", QVariantList{trackId}}};
        const OperationResult p = save(pl);
        QCOMPARE(p.code, 0);
        QVERIFY(p.id > 0);
        QCOMPARE(save(pl).code, static_cast<int>(ErrorCode::AlreadyExists));
    }

    void anArtistCarouselOpensAsACategoryPage()
    {
        // the title of a carousel on an artist card opens "section:<artist>:<kind>": it used to come back empty
        QSignalSpy cards(m_engine, &LibraryEngine::artistCardReady);
        QSignalSpy lists(m_engine, &LibraryEngine::artistsReady);
        m_engine->loadArtists(0, 0, false);
        QVERIFY(lists.count() >= 1);
        const QList<ListRow> artists = lists.last().value(0).value<QList<ListRow>>();
        qint64 withAlbums = 0;
        for (const ListRow& a : artists) {
            QSignalSpy card(m_engine, &LibraryEngine::artistCardReady);
            m_engine->loadArtistCard(a.id);
            if (card.count() && !card.value(0).value(0).value<ArtistCard>().sections.isEmpty()) {
                withAlbums = a.id;
                break;
            }
        }
        QVERIFY(withAlbums > 0);
        QSignalSpy category(m_engine, &LibraryEngine::categoryReady);
        const QString key = QStringLiteral("section:%1:discography").arg(withAlbums);
        m_engine->loadCategory(key);
        QCOMPARE(category.count(), 1);
        const CategoryContents contents = category.value(0).value(0).value<CategoryContents>();
        QCOMPARE(contents.requestedKey, key);
        QVERIFY(!contents.items.isEmpty());
        QVERIFY(!contents.sectionLabel.isEmpty());
    }

    void aTakenArtistNameIsReportedBeforeARenameMerges()
    {
        QSignalSpy spy(m_engine, &LibraryEngine::mergeTargetFound);
        QVariantMap cmd{{"type", "artist"}, {"mode", "add"}, {"name", "Merge Probe"}, {"tags", QStringList{}}, {"hidden", false}};
        const OperationResult made = save(cmd);
        QCOMPARE(made.code, 0);
        m_engine->findMergeTarget(1, QStringLiteral("merge probe"), 0);  // another name spelling, same artist
        QVERIFY(spy.last().at(1).toBool());
        m_engine->findMergeTarget(2, QStringLiteral("Merge Probe"), made.id);  // its own name is no clash
        QVERIFY(!spy.last().at(1).toBool());
        m_engine->findMergeTarget(3, QStringLiteral("Nobody Called This"), 0);
        QVERIFY(!spy.last().at(1).toBool());
    }

    void playbackWritesGoToAThreadOfTheirOwnAndArriveInTheDatabase()
    {
        const ScanSummary s = scan(makeFiles(QStringLiteral("Async"), 2, 300));
        QVariantMap cmd{{"type", "album"}, {"mode", "add"}, {"title", "Async Album"}, {"albumType", 1}, {"year", 2022}, {"tags", QStringList{}},
                        {"authors", QVariantList{QVariantMap{{"id", 0}, {"name", "Async Artist"}}}}, {"rows", rowsOf(s)}, {"startOver", true}};
        const OperationResult r = save(cmd);
        QCOMPARE(r.code, 0);
        QSignalSpy card(m_engine, &LibraryEngine::albumCardReady);
        m_engine->loadAlbumCard(r.id);
        const AlbumCard c = card.value(0).value(0).value<AlbumCard>();
        QVERIFY(!c.tracks.isEmpty());

        const QString dbPath = m_dir.path() + QStringLiteral("/library/.library/library.db");
        auto reader = Database::open(dbPath);
        QVERIFY(reader.ok());
        AsyncPlaybackStore store(dbPath, *reader.value());
        QSignalSpy recent(&store, &AsyncPlaybackStore::recentWritten);
        // the engine thread holds the database for a while (a long save): the GUI thread must not wait for it
        auto engineConnection = Database::open(dbPath);
        QVERIFY(engineConnection.ok());
        QVERIFY(engineConnection.value()->run(QStringLiteral("BEGIN IMMEDIATE")).ok());
        QElapsedTimer timer;
        timer.start();
        store.recordPlay(TrackId(c.tracks.first().id), 1700000000000);
        store.saveSession(QJsonObject{{"version", 2}});
        QVERIFY2(timer.elapsed() < 200, "a write from the GUI thread waited for the busy database");
        QVERIFY(engineConnection.value()->run(QStringLiteral("COMMIT")).ok());
        store.recordPlay(TrackId(c.tracks.first().id), 1700000000000);
        store.touchRecent(ContainerKind::Album, r.id, 1700000000000);
        store.saveSession(QJsonObject{{"version", 1}});
        store.flush();
        QTRY_COMPARE(recent.count(), 1);
        QCOMPARE(store.loadSession().value("version").toInt(), 1);  // the last write wins
        auto played = reader.value()->scalar(QStringLiteral("SELECT play_count FROM track WHERE id = ?"), {c.tracks.first().id});
        QVERIFY(played.ok());
        QCOMPARE(played.value().toInt(), 2);  // one while the database was busy, one after
    }

    void anUnreadableDropIsReportedNotFatal()
    {
        const QString junk = m_music.path() + QStringLiteral("/notes.txt");
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not music");
        f.close();
        const ScanSummary s = scan({junk});
        QCOMPARE(s.rows.size(), 0);
    }

    void deleteTextsDescribeEveryOutcome()
    {
        DeleteSummary d;
        d.op = 5;
        d.tracksDeleted = 2;
        d.tracksKept = 1;
        const QString text = deleteText(d);
        QVERIFY(text.contains(QStringLiteral("Recycle Bin")));
        QVERIFY(text.contains(QStringLiteral("elsewhere")));
        d = {};
        d.op = 6;
        d.albumsDeleted = 2;
        d.albumsRehomed = 1;
        d.artistsHidden = 1;
        QVERIFY(deleteText(d).contains(QStringLiteral("hidden")));
        QVERIFY(!deleteTitle(7).isEmpty());
    }
};

QTEST_MAIN(TstEngineEditors)
#include "tst_engine_editors.moc"
