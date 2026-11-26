#include "SessionFixture.h"

using namespace semplice;
using namespace semplice::testing;

namespace {

const QByteArray kJpeg = QByteArray::fromHex("FFD8FFE000104A46494600010100000100010000FFD9");

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

}  // namespace

// M4: ImportService on a real library folder with real (tiny) audio files.
class TstImportService : public QObject {
    Q_OBJECT

    // The source files must never change, whatever happens to the library.
    static QMap<QString, QByteArray> sourceBytes(const QString& dir)
    {
        QMap<QString, QByteArray> out;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            out.insert(p, SessionFixture::readAll(p));
        }
        return out;
    }

private slots:
    // ================================================================ scan + propose
    void scanFindsAudioHashesItAndSkipsTheRest()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("The Band"), QStringLiteral("First"), 3, 100);
        writeFile(QDir(folder).filePath(QStringLiteral("notes.txt")), "hello");
        writeFile(QDir(folder).filePath(QStringLiteral("broken.flac")), QByteArray());  // not an audio file at all

        auto scan = fx.session->importer().scan({folder});
        QVERIFY(scan.ok());
        QCOMPARE(scan.value().tracks.size(), 3);
        QCOMPARE(scan.value().skipped.size(), 1);
        for (const ScannedTrack& t : scan.value().tracks) {
            QCOMPARE(t.hash.size(), 32);
            QVERIFY(!t.duplicateOf.has_value());
            QCOMPARE(t.tags.artists, QStringList{QStringLiteral("The Band")});
        }
        // natural order: t01, t02, t03
        QVERIFY(scan.value().tracks.at(0).sourcePath.endsWith(QStringLiteral("t01.mp3")));
        QVERIFY(scan.value().tracks.at(2).sourcePath.endsWith(QStringLiteral("t03.mp3")));
    }

    void scanAcceptsLooseFilesAndFoldersTogetherWithoutRepeats()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("X"), QStringLiteral("Alb"), 2, 10);
        const QString loose = QDir(folder).filePath(QStringLiteral("t01.mp3"));
        auto scan = fx.session->importer().scan({folder, loose});
        QVERIFY(scan.ok());
        QCOMPARE(scan.value().tracks.size(), 2);
    }

    void classificationFollowsTheTrackCount_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("type");
        QTest::newRow("1") << 1 << static_cast<int>(AlbumType::Single);
        QTest::newRow("3") << 3 << static_cast<int>(AlbumType::Single);
        QTest::newRow("4") << 4 << static_cast<int>(AlbumType::Ep);
        QTest::newRow("7") << 7 << static_cast<int>(AlbumType::Ep);
        QTest::newRow("8") << 8 << static_cast<int>(AlbumType::Album);
        QTest::newRow("12") << 12 << static_cast<int>(AlbumType::Album);
    }
    void classificationFollowsTheTrackCount()
    {
        QFETCH(int, count);
        QFETCH(int, type);
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("The Band"), QStringLiteral("Rec"), count, 200,
                                              {QStringLiteral("Rock")});
        auto scan = fx.session->importer().scan({folder});
        QVERIFY(scan.ok());
        const ImportProposal p = fx.session->importer().propose(scan.value());
        QCOMPARE(p.kind, ImportKind::Album);
        QCOMPARE(static_cast<int>(p.album.type), type);
        // one track: the album is named like the track (owner decision); more: the album tag
        QCOMPARE(p.album.title, count == 1 ? p.album.tracks.first().title : QStringLiteral("Rec"));
        QCOMPARE(p.album.authors, QStringList{QStringLiteral("The Band")});
        QCOMPARE(p.album.tracks.size(), count);
        QCOMPARE(p.album.tags, QStringList{QStringLiteral("rock")});
        QCOMPARE(p.album.tracks.first().title, QStringLiteral("Rec 1"));
    }

    void unrelatedFilesBecomeAPlaylist()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.source(QStringLiteral("mix"), QStringLiteral("a"), 1, QStringLiteral("One"), {QStringLiteral("A")});
        fx.source(QStringLiteral("mix"), QStringLiteral("b"), 2, QStringLiteral("Two"), {QStringLiteral("B")});
        fx.source(QStringLiteral("mix"), QStringLiteral("c"), 3, QStringLiteral("Three"), {QStringLiteral("C")});
        auto scan = fx.session->importer().scan({QDir(fx.srcDir()).filePath(QStringLiteral("mix"))});
        QVERIFY(scan.ok());
        const ImportProposal p = fx.session->importer().propose(scan.value());
        QCOMPARE(p.kind, ImportKind::Playlist);
        QCOMPARE(p.playlist.tracks.size(), 3);
    }

    void aPreselectedArtistMakesAnAlbumOfLooseFiles()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.source(QStringLiteral("mix"), QStringLiteral("a"), 1, QStringLiteral("One"), {QStringLiteral("A")});
        fx.source(QStringLiteral("mix"), QStringLiteral("b"), 2, QStringLiteral("Two"), {QStringLiteral("B")});
        const ArtistId artist = fx.session->library().createArtist({QStringLiteral("Open Artist"), false, {}, {}}).value();
        auto scan = fx.session->importer().scan({QDir(fx.srcDir()).filePath(QStringLiteral("mix"))});
        const ImportProposal p = fx.session->importer().propose(scan.value(), artist);
        QCOMPARE(p.kind, ImportKind::Album);
        QCOMPARE(p.album.authors.value(0), QStringLiteral("Open Artist"));
    }

    void commaSeparatedArtistsOnEveryTrackAreAllAlbumArtists()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.source(QStringLiteral("duo"), QStringLiteral("a"), 3, QStringLiteral("Song"), {QStringLiteral("Artist One, Artist Two")},
                  QStringLiteral("Together"), 1);
        auto scan = fx.session->importer().scan({QDir(fx.srcDir()).filePath(QStringLiteral("duo"))});
        const ImportProposal p = fx.session->importer().propose(scan.value());
        QCOMPARE(p.kind, ImportKind::Album);
        QCOMPARE(p.album.authors, (QStringList{QStringLiteral("Artist One"), QStringLiteral("Artist Two")}));  // both get a profile
        QCOMPARE(p.album.tracks.first().credits.size(), 2);
    }

    void aSingleTrackMakesAnAlbumNamedLikeTheTrack()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.source(QStringLiteral("one"), QStringLiteral("a"), 1, QStringLiteral("Miss You"), {QStringLiteral("Solo Artist")}, QStringLiteral("Some Other Album"), 1);
        auto scan = fx.session->importer().scan({QDir(fx.srcDir()).filePath(QStringLiteral("one"))});
        const ImportProposal p = fx.session->importer().propose(scan.value());
        QCOMPARE(p.kind, ImportKind::Album);
        QCOMPARE(p.album.title, p.album.tracks.first().title);
        QCOMPARE(p.album.type, AlbumType::Single);
    }

    void featuredArtistsAreSplitIntoCredits()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.source(QStringLiteral("f"), QStringLiteral("a"), 1, QStringLiteral("Song"), {QStringLiteral("Main Guy feat. Other Guy")},
                  QStringLiteral("Alb"), 1);
        auto scan = fx.session->importer().scan({QDir(fx.srcDir()).filePath(QStringLiteral("f"))});
        const ImportProposal p = fx.session->importer().propose(scan.value());
        QCOMPARE(p.kind, ImportKind::Album);
        const auto& credits = p.album.tracks.first().credits;
        QCOMPARE(credits.size(), 2);
        QCOMPARE(credits.at(0).name, QStringLiteral("Main Guy"));
        QCOMPARE(credits.at(0).role, CreditRole::Main);
        QCOMPARE(credits.at(1).name, QStringLiteral("Other Guy"));
        QCOMPARE(credits.at(1).role, CreditRole::Feat);
    }

    // ================================================================ commit
    void importingAnAlbumCopiesTagsAndRecordsEverything()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("The Band"), QStringLiteral("First Record"), 5, 300,
                                              {QStringLiteral("Rock")});
        writeFile(QDir(folder).filePath(QStringLiteral("cover.jpg")), kJpeg);
        const auto before = sourceBytes(fx.srcDir());

        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        QVERIFY(scan.ok());
        ImportProposal p = importer.propose(scan.value());
        QCOMPARE(p.kind, ImportKind::Album);
        QVERIFY(p.album.coverPath.endsWith(QStringLiteral("cover.jpg")));

        auto done = importer.commitAlbum(scan.value(), p.album);
        QVERIFY2(done.ok(), done.ok() ? "" : qPrintable(done.error().symbol() + done.error().devMessage));
        QCOMPARE(done.value().imported, 5);
        QCOMPARE(done.value().duplicates, 0);

        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/First Record/01 - First Record 1.mp3"))));
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/First Record/album_cover.jpg"))));
        QCOMPARE(sourceBytes(fx.srcDir()), before);  // sources untouched
        QCOMPARE(fx.count(QStringLiteral("track")), 5);
        QCOMPARE(fx.count(QStringLiteral("album")), 1);
        QCOMPARE(fx.verify(), QString());

        // Tags inside the copies carry the album facts.
        auto tags = fx.session->tags().read(fx.abs(QStringLiteral("Artists/The Band/First Record/03 - First Record 3.mp3")));
        QVERIFY(tags.ok());
        QCOMPARE(tags.value().album, QStringLiteral("First Record"));
        QCOMPARE(tags.value().trackNumber, 3);
        QVERIFY(tags.value().hasCover);
    }

    void importingAPlaylistPutsFilesUnderPlaylists()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.source(QStringLiteral("mix"), QStringLiteral("a"), 1, QStringLiteral("One"), {QStringLiteral("A")});
        fx.source(QStringLiteral("mix"), QStringLiteral("b"), 2, QStringLiteral("Two"), {QStringLiteral("B & C")});
        auto& importer = fx.session->importer();
        auto scan = importer.scan({QDir(fx.srcDir()).filePath(QStringLiteral("mix"))});
        ImportProposal p = importer.propose(scan.value());
        p.playlist.name = QStringLiteral("Road trip");
        auto done = importer.commitPlaylist(scan.value(), p.playlist);
        QVERIFY2(done.ok(), done.ok() ? "" : qPrintable(done.error().symbol() + done.error().devMessage));
        QCOMPARE(fx.count(QStringLiteral("playlist")), 1);
        QCOMPARE(fx.count(QStringLiteral("track")), 2);
        for (const QString& f : fx.filesInDatabase())
            QVERIFY2(f.startsWith(QStringLiteral("Playlists/Road trip/")), qPrintable(f));
        QCOMPARE(fx.verify(), QString());
        // the artists exist but stay hidden (they have no album of their own)
        const auto a = fx.artistNamed(QStringLiteral("A"));
        QVERIFY(a.has_value());
        QVERIFY(a->hidden);
    }

    void importingTheSameFilesAgainOnlyAddsThemToTheNewContainer()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("The Band"), QStringLiteral("First"), 4, 400);
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        QVERIFY(importer.commitAlbum(scan.value(), importer.propose(scan.value()).album).ok());
        const QSet<QString> filesAfterFirst = fx.filesOnDisk();

        auto again = importer.scan({folder});
        QVERIFY(again.ok());
        for (const ScannedTrack& t : again.value().tracks)
            QVERIFY(t.duplicateOf.has_value());
        ImportPlaylistForm form;
        form.name = QStringLiteral("Same songs");
        for (int i = 0; i < again.value().tracks.size(); ++i) {
            ImportTrackForm tf;
            tf.scanIndex = i;
            tf.title = again.value().tracks.at(i).tags.title;
            tf.credits = {CreditDraft(QStringLiteral("The Band"))};
            form.tracks.append(tf);
        }
        auto done = importer.commitPlaylist(again.value(), form);
        QVERIFY(done.ok());
        QCOMPARE(done.value().imported, 0);
        QCOMPARE(done.value().duplicates, 4);
        QCOMPARE(fx.filesOnDisk(), filesAfterFirst);  // nothing was copied again
        QCOMPARE(fx.count(QStringLiteral("track")), 4);
        QCOMPARE(fx.verify(), QString());
    }

    void droppingFilesOntoAnAlbumAppendsThem()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("The Band"), QStringLiteral("First"), 4, 500);
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        auto first = importer.commitAlbum(scan.value(), importer.propose(scan.value()).album);
        QVERIFY(first.ok());
        const AlbumId album = first.value().album;

        const QString extra = fx.source(QStringLiteral("extra"), QStringLiteral("bonus"), 999, QStringLiteral("Bonus"),
                                        {QStringLiteral("The Band")});
        auto scan2 = importer.scan({extra});
        ImportTrackForm tf;
        tf.scanIndex = 0;
        tf.title = QStringLiteral("Bonus");
        tf.credits = {CreditDraft(QStringLiteral("The Band"))};
        auto done = importer.appendToAlbum(album, scan2.value(), {tf});
        QVERIFY2(done.ok(), done.ok() ? "" : qPrintable(done.error().symbol() + done.error().devMessage));
        AlbumRepo albums(fx.session->database());
        QCOMPARE(albums.tracks(album).value().size(), 5);
        QCOMPARE(fx.verify(), QString());
    }

    void anEmbeddedCoverBecomesTheAlbumCover()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString path = fx.source(QStringLiteral("a"), QStringLiteral("t01"), 7, QStringLiteral("Song"), {QStringLiteral("Solo")},
                                       QStringLiteral("Solo Album"), 1);
        TagWrite w;
        w.title = QStringLiteral("Song");
        w.artists = {QStringLiteral("Solo")};
        w.album = QStringLiteral("Solo Album");
        w.albumArtists = QStringList{QStringLiteral("Solo")};
        w.date = ReleaseDate{2021, 3, 4};
        w.trackNumber = 1;
        w.coverAction = CoverAction::Set;
        w.cover = *CoverImage::fromBytes(kJpeg);
        QVERIFY(fx.session->tags().write(path, w).ok());

        auto& importer = fx.session->importer();
        auto scan = importer.scan({path});
        const ImportProposal p = importer.propose(scan.value());
        QVERIFY(p.album.coverPath.isEmpty());
        QVERIFY(p.album.embeddedCover.has_value());
        QVERIFY(importer.commitAlbum(scan.value(), p.album).ok());
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/Solo/%1/album_cover.jpg").arg(p.album.title))));
        QCOMPARE(fx.verify(), QString());
        // the temporary extraction was cleaned up
        QDir tmp(fx.abs(QStringLiteral(".library/tmp")));
        QVERIFY(!tmp.exists() || tmp.isEmpty());
    }

    // ================================================================ cancel + crash
    void cancellingAtAnyPointLeavesNothingBehind()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("The Band"), QStringLiteral("First"), 5, 600);
        writeFile(QDir(folder).filePath(QStringLiteral("cover.jpg")), kJpeg);
        const auto before = sourceBytes(fx.srcDir());
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        const ImportProposal p = importer.propose(scan.value());

        bool finished = false;
        for (int stopAt = 1; stopAt < 60 && !finished; ++stopAt) {
            std::stop_source source;
            int calls = 0;
            JobContext job;
            job.cancel = source.get_token();
            job.progress = [&](const JobProgress&) {
                if (++calls == stopAt)
                    source.request_stop();
            };
            auto r = importer.commitAlbum(scan.value(), p.album, job);
            if (r.ok()) {
                finished = true;
                break;
            }
            QCOMPARE(r.error().code, ErrorCode::Cancelled);
            QCOMPARE(fx.count(QStringLiteral("track")), 0);
            QCOMPARE(fx.count(QStringLiteral("album")), 0);
            QCOMPARE(fx.count(QStringLiteral("artist")), 0);
            QVERIFY2(fx.filesOnDisk().isEmpty(), qPrintable(QStringList(fx.filesOnDisk().begin(), fx.filesOnDisk().end()).join(',')));
            QCOMPARE(fx.verify(), QString());
            QCOMPARE(sourceBytes(fx.srcDir()), before);
        }
        QVERIFY2(finished, "the import never completed");
        QCOMPARE(fx.count(QStringLiteral("track")), 5);
        QCOMPARE(fx.verify(), QString());
    }

    void aCrashAtAnyCheckpointIsAllOrNothingAfterRecovery()
    {
        for (int target = 1; target < 200; ++target) {
            SessionFixture fx;
            QVERIFY(fx.create());
            const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("The Band"), QStringLiteral("First"), 3, 700);
            fx.session.reset();
            CrashAt crash(target);
            QVERIFY(fx.open(&crash));
            auto& importer = fx.session->importer();
            auto scan = importer.scan({folder});
            const ImportProposal p = importer.propose(scan.value());
            auto r = importer.commitAlbum(scan.value(), p.album);
            if (r.ok()) {  // the crash point lies beyond the last checkpoint: every earlier one was tried
                QVERIFY2(target > 3, "too few checkpoints were exercised");
                return;
            }
            QCOMPARE(r.error().code, ErrorCode::CrashInjected);
            QVERIFY(fx.open());  // "restart": recovery runs
            const int tracks = fx.count(QStringLiteral("track"));
            QVERIFY2(tracks == 0 || tracks == 3, qPrintable(QStringLiteral("target %1 (%2): %3 tracks").arg(target).arg(crash.where()).arg(tracks)));
            QVERIFY2(fx.verify().isEmpty(), qPrintable(QStringLiteral("target %1 (%2): %3").arg(target).arg(crash.where(), fx.verify())));
            if (tracks == 0)
                QCOMPARE(fx.count(QStringLiteral("album")), 0);
        }
        QFAIL("the import never completed within 200 checkpoints");
    }

    void aLockedPlayerFileDoesNotStopAnEdit()
    {
        // (the player is only involved for EXISTING files, so a plain import still works while it refuses)
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.releaser.refuse = true;
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("The Band"), QStringLiteral("First"), 2, 800);
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        auto done = importer.commitAlbum(scan.value(), importer.propose(scan.value()).album);
        QVERIFY(done.ok());
        QCOMPARE(fx.verify(), QString());
    }
};

QTEST_GUILESS_MAIN(TstImportService)
#include "tst_import_service.moc"
