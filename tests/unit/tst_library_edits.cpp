#include "SessionFixture.h"

using namespace semplice;
using namespace semplice::testing;

namespace {
const QByteArray kJpeg = QByteArray::fromHex("FFD8FFE000104A46494600010100000100010000FFD9");
const QByteArray kPng = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==");

QString writeImage(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(data);
    return path;
}
}  // namespace

// M4: edits, renames, deletes and tag sync against real files. After every step the files on disk,
// the database rows and the tags inside the files must agree (SessionFixture::verify).
class TstLibraryEdits : public QObject {
    Q_OBJECT

    // Imports an album "<album>" by "<artist>" with `count` tracks and returns its id.
    static AlbumId importAlbum(SessionFixture& fx, const QString& artist, const QString& album, int count, int seed,
                               const QString& subfolder = {})
    {
        const QString folder = fx.albumFolder(subfolder.isEmpty() ? album : subfolder, artist, album, count, seed);
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        QVERIFY2_RETURN(scan.ok(), "scan failed", AlbumId());
        auto done = importer.commitAlbum(scan.value(), importer.propose(scan.value()).album);
        QVERIFY2_RETURN(done.ok(), done.ok() ? QString() : done.error().symbol() + done.error().devMessage, AlbumId());
        return done.value().album;
    }
    static ArtistId artistId(SessionFixture& fx, const QString& name)
    {
        const auto a = fx.artistNamed(name);
        return a ? a->id : ArtistId();
    }
    static TrackId trackAt(SessionFixture& fx, AlbumId album, int position)
    {
        AlbumRepo repo(fx.session->database());
        return repo.tracks(album).value().value(position - 1);
    }

private slots:
    void renamingAnArtistToAnExistingNameMergesTheDiscographies()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId a1 = importAlbum(fx, QStringLiteral("Alpha"), QStringLiteral("First Record"), 2, 80);
        const AlbumId b1 = importAlbum(fx, QStringLiteral("Beta"), QStringLiteral("Second Record"), 2, 90);
        const ArtistId alpha = artistId(fx, QStringLiteral("Alpha"));
        const ArtistId beta = artistId(fx, QStringLiteral("Beta"));
        QVERIFY(alpha.isValid() && beta.isValid());

        // Beta is renamed "Alpha": the two become one artist, Beta (the one being renamed) wins
        ArtistPatch patch;
        patch.name = QStringLiteral("Alpha");
        const auto merged = fx.session->library().updateArtist(beta, patch);
        QVERIFY2(merged.ok(), merged.ok() ? "" : qPrintable(merged.error().symbol() + QStringLiteral(": ") + merged.error().devMessage));

        QVERIFY(fx.artistNamed(QStringLiteral("Alpha")).has_value());
        QCOMPARE(fx.artistNamed(QStringLiteral("Alpha"))->id, beta);
        AlbumRepo albums(fx.session->database());
        QCOMPARE(albums.get(a1).value()->primaryArtist, beta);
        QCOMPARE(albums.get(b1).value()->primaryArtist, beta);
        const auto old = ArtistRepo(fx.session->database()).get(alpha);
        QVERIFY(!old.value().has_value());  // the old Alpha is gone
        // every track of both albums is credited to the surviving artist
        for (AlbumId a : {a1, b1}) {
            const auto tracks = albums.tracks(a).value();
            QVERIFY(!tracks.isEmpty());
            for (TrackId t : tracks) {
                const auto credits = TrackRepo(fx.session->database()).credits(t).value();
                QVERIFY(!credits.isEmpty());
                QCOMPARE(credits.first().artist, beta);
            }
        }
        QCOMPARE(fx.verify(), QString());  // files, folders, rows and tags agree
    }

    void renamingToTheNameOfAHiddenArtistTakesOverItsCredits()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("Solo"), QStringLiteral("Record"), 2, 100);
        auto hidden = fx.session->library().createArtist({QStringLiteral("Guest Star"), true, {}, {}});
        QVERIFY(hidden.ok());
        const TrackId t = trackAt(fx, album, 1);
        const ArtistId solo = artistId(fx, QStringLiteral("Solo"));
        TrackPatch tp;
        tp.credits = QList<CreditDraft>{CreditDraft(solo), CreditDraft(hidden.value(), CreditRole::Feat)};
        QVERIFY(fx.session->library().updateTrack(t, tp).ok());

        ArtistPatch patch;
        patch.name = QStringLiteral("Guest Star");
        const auto merged = fx.session->library().updateArtist(solo, patch);
        QVERIFY2(merged.ok(), merged.ok() ? "" : qPrintable(merged.error().symbol() + QStringLiteral(": ") + merged.error().devMessage));
        QVERIFY(!ArtistRepo(fx.session->database()).get(hidden.value()).value().has_value());
        QCOMPARE(fx.artistNamed(QStringLiteral("Guest Star"))->id, solo);
        const auto credits = TrackRepo(fx.session->database()).credits(t).value();
        QCOMPARE(credits.size(), 1);  // not credited twice
        QCOMPARE(credits.first().artist, solo);
        QCOMPARE(fx.verify(), QString());
    }

    void renamesAndDeletesLetTheAudioBackendGoOfTheFolder()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("Old Name"), 2, 70);
        fx.releaser.released.clear();
        fx.releaser.ready.clear();
        AlbumPatch patch;
        patch.title = QStringLiteral("New Name");
        QVERIFY(fx.session->library().updateAlbum(album, patch).ok());
        // the folder of the album was moved: the player was asked to let go of it, and given it back afterwards
        QVERIFY2(!fx.releaser.released.isEmpty(), "nothing was released before the rename");
        QVERIFY(std::any_of(fx.releaser.released.cbegin(), fx.releaser.released.cend(), [](const QString& p) { return p.endsWith(QStringLiteral("Old Name")); }));
        QCOMPARE(fx.releaser.ready.size(), fx.releaser.released.size());
        QCOMPARE(fx.verify(), QString());

        fx.releaser.released.clear();
        fx.releaser.ready.clear();
        QVERIFY(fx.session->library().deleteAlbum(album).ok());
        QVERIFY(!fx.releaser.released.isEmpty());  // the files went to the Recycle Bin: released first
        QCOMPARE(fx.releaser.ready.size(), fx.releaser.released.size());
    }

    // ================================================================ renames
    void renamingAnArtistMovesTheFolderAndRetagsEveryFile()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
        importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("Second"), 2, 50);
        QCOMPARE(fx.verify(), QString());

        ArtistPatch patch;
        patch.name = QStringLiteral("New Name");
        auto r = fx.session->library().updateArtist(artistId(fx, QStringLiteral("The Band")), patch);
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol() + r.error().devMessage));
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/New Name/First/01 - First 1.mp3"))));
        QVERIFY(!QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band"))));
        QCOMPARE(fx.verify(), QString());  // includes: the artist tag of every file now says "New Name"
        auto tags = fx.session->tags().read(fx.abs(QStringLiteral("Artists/New Name/Second/02 - Second 2.mp3")));
        QCOMPARE(tags.value().artists, QStringList{QStringLiteral("New Name")});
    }

    void aCaseOnlyRenameWorks()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("the band"), QStringLiteral("first"), 2, 10);
        ArtistPatch patch;
        patch.name = QStringLiteral("The Band");
        QVERIFY(fx.session->library().updateArtist(artistId(fx, QStringLiteral("the band")), patch).ok());
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/first/01 - first 1.mp3"))));
        QCOMPARE(fx.verify(), QString());
        AlbumPatch ap;
        ap.title = QStringLiteral("First");
        AlbumRepo albums(fx.session->database());
        const AlbumId album = albums.all().value().first().id;
        QVERIFY(fx.session->library().updateAlbum(album, ap).ok());
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/First/01 - First 1.mp3"))));
        QCOMPARE(fx.verify(), QString());
    }

    void renamingAnAlbumRenamesFolderFilesCoverAndTags()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
        AlbumPatch cover;
        cover.coverPath = writeImage(fx.dirPath(QStringLiteral("c.jpg")), kJpeg);
        QVERIFY(fx.session->library().updateAlbum(album, cover).ok());
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/First/album_cover.jpg"))));
        QCOMPARE(fx.verify(), QString());

        AlbumPatch patch;
        patch.title = QStringLiteral("Renamed: Part 1?");  // characters Windows does not allow are replaced
        auto r = fx.session->library().updateAlbum(album, patch);
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol() + r.error().devMessage));
        const auto row = fx.albumRow(album);
        QVERIFY(row.has_value());
        QCOMPARE(row->title, QStringLiteral("Renamed: Part 1?"));
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/") + row->dirName)));
        QVERIFY(!QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/First"))));
        QVERIFY(row->coverPath.startsWith(QStringLiteral("Artists/The Band/")) && row->coverPath.contains(QStringLiteral("album_cover")));
        QCOMPARE(fx.verify(), QString());
    }

    void changingAlbumDataRewritesTheTags()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
        AlbumPatch patch;
        patch.date = ReleaseDate{1999, 12, 31};
        patch.tags = QStringList{QStringLiteral("rock"), QStringLiteral("pop")};
        patch.type = AlbumType::Ep;
        QVERIFY(fx.session->library().updateAlbum(album, patch).ok());
        QCOMPARE(fx.verify(), QString());
        auto tags = fx.session->tags().read(fx.abs(QStringLiteral("Artists/The Band/First/01 - First 1.mp3")));
        QCOMPARE(tags.value().date.year, 1999);
    }

    void renamingATrackRenamesItsFileAndTitleTag()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
        TrackPatch patch;
        patch.title = QStringLiteral("Brand New Title");
        const TrackId t = trackAt(fx, album, 2);
        auto r = fx.session->library().updateTrack(t, patch);
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol() + r.error().devMessage));
        QCOMPARE(fx.trackRow(t).filePath, QStringLiteral("Artists/The Band/First/02 - Brand New Title.mp3"));
        QCOMPARE(fx.verify(), QString());
    }

    void reorderingTheTracksRenumbersFilesAndTags()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 4, 10);
        AlbumRepo albums(fx.session->database());
        QList<TrackId> order = albums.tracks(album).value();
        std::reverse(order.begin(), order.end());
        AlbumPatch patch;
        patch.trackOrder = order;
        QVERIFY(fx.session->library().updateAlbum(album, patch).ok());
        QCOMPARE(albums.tracks(album).value(), order);
        QCOMPARE(fx.trackRow(order.first()).filePath, QStringLiteral("Artists/The Band/First/01 - First 4.mp3"));
        QCOMPARE(fx.verify(), QString());
        auto tags = fx.session->tags().read(fx.abs(QStringLiteral("Artists/The Band/First/01 - First 4.mp3")));
        QCOMPARE(tags.value().trackNumber, 1);
    }

    void changingTheCreditsOfATrackCleansUpHiddenArtists()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        const TrackId t = trackAt(fx, album, 1);
        TrackPatch add;
        add.credits = QList<CreditDraft>{CreditDraft(artistId(fx, QStringLiteral("The Band"))),
                                         CreditDraft(QStringLiteral("Guest"), CreditRole::Feat)};
        QVERIFY(fx.session->library().updateTrack(t, add).ok());
        const auto guest = fx.artistNamed(QStringLiteral("Guest"));
        QVERIFY(guest.has_value());
        QVERIFY(guest->hidden);
        QCOMPARE(fx.verify(), QString());
        auto tags = fx.session->tags().read(fx.abs(fx.trackRow(t).filePath));
        QCOMPARE(tags.value().artists, (QStringList{QStringLiteral("The Band"), QStringLiteral("Guest")}));

        TrackPatch remove;
        remove.credits = QList<CreditDraft>{CreditDraft(artistId(fx, QStringLiteral("The Band")))};
        QVERIFY(fx.session->library().updateTrack(t, remove).ok());
        QVERIFY(!fx.artistNamed(QStringLiteral("Guest")).has_value());  // nobody needs the hidden artist any more
        QCOMPARE(fx.verify(), QString());
    }

    void changingTheAuthorMovesTheAlbumFolder()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("Old Owner"), QStringLiteral("Shared"), 2, 10);
        const ArtistId other = fx.session->library().createArtist({QStringLiteral("New Owner"), false, {}, {}}).value();
        AlbumPatch patch;
        patch.primaryArtist = other;
        auto r = fx.session->library().updateAlbum(album, patch);
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol() + r.error().devMessage));
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/New Owner/Shared/01 - Shared 1.mp3"))));
        QCOMPARE(fx.verify(), QString());
    }

    // ================================================================ tag sync failures
    void aLockedFileKeepsTheEditAndIsRetriedByRescan()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 2, 10);
        fx.releaser.refuse = true;  // the player holds the files
        TrackPatch patch;
        patch.title = QStringLiteral("Changed Title");
        const TrackId t = trackAt(fx, album, 1);
        auto r = fx.session->library().updateTrack(t, patch);
        QVERIFY(r.ok());  // the edit itself succeeds
        QCOMPARE(r.value().tagWriteFailed, QList<TrackId>{t});
        QVERIFY(fx.trackRow(t).tagsDirty);
        QCOMPARE(fx.trackRow(t).title, QStringLiteral("Changed Title"));
        QCOMPARE(fx.verify(), QString());  // dirty tracks are skipped by the tag comparison, the rest must hold

        auto still = fx.session->rescanner().run();
        QVERIFY(still.ok());
        QCOMPARE(still.value().tagsFailed, 1);
        QVERIFY(fx.trackRow(t).tagsDirty);

        fx.releaser.refuse = false;
        auto fixed = fx.session->rescanner().run();
        QVERIFY(fixed.ok());
        QCOMPARE(fixed.value().tagsSynced, 1);
        QCOMPARE(fixed.value().tagsFailed, 0);
        QVERIFY(!fx.trackRow(t).tagsDirty);
        QCOMPARE(fx.verify(), QString());
        auto tags = fx.session->tags().read(fx.abs(fx.trackRow(t).filePath));
        QCOMPARE(tags.value().title, QStringLiteral("Changed Title"));
    }

    // ================================================================ deletes
    void deletingTracksAndAlbumsSendsFilesToTheRecycleBin()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
        const AlbumId other = importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("Second"), 2, 50);

        auto r = fx.session->library().deleteTrack(trackAt(fx, album, 1));
        QVERIFY(r.ok());
        QCOMPARE(fx.count(QStringLiteral("track")), 4);
        QCOMPARE(fx.trash.items().size(), 1);
        QCOMPARE(fx.verify(), QString());

        auto a = fx.session->library().deleteAlbum(other);
        QVERIFY(a.ok());
        QCOMPARE(fx.count(QStringLiteral("track")), 2);
        QVERIFY(!QFileInfo::exists(fx.abs(QStringLiteral("Artists/The Band/Second"))));
        QCOMPARE(fx.verify(), QString());
        // nothing was lost for good: the files are in the (fake) Recycle Bin
        QVERIFY(fx.trash.items().size() >= 2);
    }

    void deletingAnArtistWhoIsNotNeededRemovesTheirWholeFolder()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        importAlbum(fx, QStringLiteral("Keep"), QStringLiteral("K"), 2, 10);
        importAlbum(fx, QStringLiteral("Gone"), QStringLiteral("G1"), 2, 50);
        importAlbum(fx, QStringLiteral("Gone"), QStringLiteral("G2"), 2, 90);
        const ArtistId gone = artistId(fx, QStringLiteral("Gone"));

        auto plan = fx.session->library().planDeleteArtist(gone);
        QVERIFY(plan.ok());
        QCOMPARE(fx.verify(), QString());  // a plan changes nothing
        QCOMPARE(fx.count(QStringLiteral("track")), 6);

        auto done = fx.session->library().deleteArtist(gone);
        QVERIFY(done.ok());
        QCOMPARE(plan.value(), done.value());
        QCOMPARE(done.value().artistsDeleted, QList<ArtistId>{gone});
        QCOMPARE(fx.count(QStringLiteral("track")), 2);
        QVERIFY(!QFileInfo::exists(fx.abs(QStringLiteral("Artists/Gone"))));
        QCOMPARE(fx.verify(), QString());
    }

    void deletingAnArtistStillCreditedElsewhereKeepsThemHidden()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId album = importAlbum(fx, QStringLiteral("Solo"), QStringLiteral("Own"), 2, 10);
        // a playlist that holds one of Solo's tracks plus an unrelated file
        const QString extra = fx.source(QStringLiteral("e"), QStringLiteral("x"), 777, QStringLiteral("Extra"), {QStringLiteral("Other")});
        auto& importer = fx.session->importer();
        auto scan = importer.scan({extra});
        ImportPlaylistForm form;
        form.name = QStringLiteral("Mix");
        ImportTrackForm tf;
        tf.scanIndex = 0;
        tf.title = QStringLiteral("Extra");
        tf.credits = {CreditDraft(QStringLiteral("Other"))};
        form.tracks = {tf};
        auto mix = importer.commitPlaylist(scan.value(), form);
        QVERIFY(mix.ok());
        QVERIFY(fx.session->library().addTrackToPlaylist(trackAt(fx, album, 1), mix.value().playlist).ok());

        const ArtistId solo = artistId(fx, QStringLiteral("Solo"));
        auto plan = fx.session->library().planDeleteArtist(solo);
        auto done = fx.session->library().deleteArtist(solo);
        QVERIFY(plan.ok() && done.ok());
        QCOMPARE(plan.value(), done.value());
        QCOMPARE(done.value().artistsHidden, QList<ArtistId>{solo});
        QVERIFY(fx.artistNamed(QStringLiteral("Solo"))->hidden);
        QVERIFY(!fx.albumRow(album).has_value());
        // the shared track survives, its file moved to the playlist folder
        QCOMPARE(fx.count(QStringLiteral("track")), 2);
        QVERIFY(fx.trackRow(TrackId{1}).filePath.startsWith(QStringLiteral("Playlists/Mix/")) ||
                fx.trackRow(TrackId{2}).filePath.startsWith(QStringLiteral("Playlists/Mix/")));
        QCOMPARE(fx.verify(), QString());
    }

    void deletingThePrimaryOfACollaborationMovesTheAlbumToACoAuthor()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const ArtistId b = fx.session->library().createArtist({QStringLiteral("Bee"), false, {}, {}}).value();
        const QString folder = fx.albumFolder(QStringLiteral("collab"), QStringLiteral("Ay"), QStringLiteral("Together"), 2, 10);
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        ImportProposal p = importer.propose(scan.value());
        p.album.authors = {QStringLiteral("Ay"), QStringLiteral("Bee")};
        auto done = importer.commitAlbum(scan.value(), p.album);
        QVERIFY(done.ok());
        QCOMPARE(fx.verify(), QString());

        auto r = fx.session->library().deleteArtist(artistId(fx, QStringLiteral("Ay")));
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol() + r.error().devMessage));
        QCOMPARE(r.value().albumsRehomed, QList<AlbumId>{done.value().album});
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists/Bee/Together/01 - Together 1.mp3"))));
        QVERIFY(!QFileInfo::exists(fx.abs(QStringLiteral("Artists/Ay"))));
        Q_UNUSED(b)
        QCOMPARE(fx.verify(), QString());
    }

    void aLongSequenceKeepsEverythingConsistent()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const AlbumId a1 = importAlbum(fx, QStringLiteral("One"), QStringLiteral("A1"), 3, 10);
        QCOMPARE(fx.verify(), QString());
        const AlbumId a2 = importAlbum(fx, QStringLiteral("Two"), QStringLiteral("A2"), 3, 50);
        QCOMPARE(fx.verify(), QString());
        AlbumPatch rename;
        rename.title = QStringLiteral("A1 renamed");
        QVERIFY(fx.session->library().updateAlbum(a1, rename).ok());
        QCOMPARE(fx.verify(), QString());
        ArtistPatch artist;
        artist.name = QStringLiteral("Two Two");
        QVERIFY(fx.session->library().updateArtist(artistId(fx, QStringLiteral("Two")), artist).ok());
        QCOMPARE(fx.verify(), QString());
        QVERIFY(fx.session->library().deleteTrack(trackAt(fx, a2, 2)).ok());
        QCOMPARE(fx.verify(), QString());
        QVERIFY(fx.session->library().deleteAlbum(a1).ok());
        QCOMPARE(fx.verify(), QString());
        QVERIFY(fx.session->library().deleteArtist(artistId(fx, QStringLiteral("Two Two"))).ok());
        QCOMPARE(fx.verify(), QString());
        QCOMPARE(fx.count(QStringLiteral("track")), 0);
        QVERIFY(fx.filesOnDisk().isEmpty());
        QCOMPARE(fx.count(QStringLiteral("artist")), 1);  // "One" stays: deleting an album never deletes its artist
    }

    void aCrashDuringARenameIsAllOrNothing()
    {
        for (int target = 1; target < 200; ++target) {
            SessionFixture fx;
            QVERIFY(fx.create());
            importAlbum(fx, QStringLiteral("The Band"), QStringLiteral("First"), 3, 10);
            fx.session.reset();
            CrashAt crash(target);
            QVERIFY(fx.open(&crash));
            ArtistPatch patch;
            patch.name = QStringLiteral("Other Name");
            auto r = fx.session->library().updateArtist(artistId(fx, QStringLiteral("The Band")), patch);
            if (r.ok()) {
                QVERIFY2(target > 3, "too few checkpoints were exercised");
                return;
            }
            QCOMPARE(r.error().code, ErrorCode::CrashInjected);
            QVERIFY(fx.open());
            // either the old name everywhere or the new one everywhere
            const bool renamed = fx.artistNamed(QStringLiteral("Other Name")).has_value();
            QCOMPARE(fx.artistNamed(QStringLiteral("The Band")).has_value(), !renamed);
            QCOMPARE(QFileInfo::exists(fx.abs(QStringLiteral("Artists/Other Name"))), renamed);
            const QString problems = fx.verify(/*checkTags=*/false);
            QVERIFY2(problems.isEmpty(), qPrintable(QStringLiteral("target %1 (%2): %3").arg(target).arg(crash.where(), problems)));
            // tags catch up with a rescan (they may be behind after a crash between commit and tag sync)
            QVERIFY(fx.session->rescanner().run().ok());
            QVERIFY2(fx.verify().isEmpty(), qPrintable(QStringLiteral("target %1 after rescan: %2").arg(target).arg(fx.verify())));
        }
        QFAIL("the rename never completed within 200 checkpoints");
    }
};

QTEST_GUILESS_MAIN(TstLibraryEdits)
#include "tst_library_edits.moc"
