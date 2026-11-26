#include "SessionFixture.h"

#include "core/share/SmplFile.h"

using namespace semplice;
using namespace semplice::testing;

// M12: the .smpl container and sharing an album / a playlist between two libraries.
class TstShare : public QObject {
    Q_OBJECT

    static void writeFile(const QString& path, const QByteArray& data)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    // an album of `count` tracks imported into the fixture's library
    static AlbumId importAlbum(SessionFixture& fx, const QString& artist, const QString& album, int count, int seed)
    {
        const QString folder = fx.albumFolder(album, artist, album, count, seed, {QStringLiteral("Rock")});
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        if (!scan.ok())
            return {};
        const ImportProposal p = importer.propose(scan.value());
        auto done = importer.commitAlbum(scan.value(), p.album);
        if (!done.ok())
            return {};
        auto found = AlbumRepo(fx.session->database()).all();
        for (const Album& a : found.value())
            if (a.title == album)
                return a.id;
        return {};
    }

    static ShareSelection everything(const ShareTree& tree)
    {
        ShareSelection s;
        s.kind = tree.kind;
        s.id = tree.id;
        for (const ShareAlbumNode& a : tree.albums) {
            s.albums.insert(a.id);
            for (const ShareTrackNode& t : a.tracks)
                s.tracks.insert(t.id);
        }
        for (const ShareTrackNode& t : tree.tracks)
            s.tracks.insert(t.id);
        for (const ShareArtistNode& a : tree.artists)
            s.avatarArtists.insert(a.id);
        return s;
    }

private slots:
    // ================================================================ the container
    void aFileWithBlobsReadsBackExactly()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath(QStringLiteral("a.bin"));
        const QString b = dir.filePath(QStringLiteral("b.bin"));
        writeFile(a, QByteArray(3000000, 'x'));
        writeFile(b, "small");
        SmplWriter writer;
        QVERIFY(writer.addFile(QStringLiteral("one"), QStringLiteral("a.bin"), a).ok());
        QVERIFY(writer.addFile(QStringLiteral("two"), QStringLiteral("b.bin"), b).ok());
        const QString path = dir.filePath(QStringLiteral("x.smpl"));
        QVERIFY(writer.write(path, QJsonObject{{QStringLiteral("name"), QStringLiteral("Test")}}).ok());

        SmplReader reader;
        QVERIFY(reader.open(path).ok());
        QCOMPARE(reader.manifest().value(QStringLiteral("name")).toString(), QStringLiteral("Test"));
        QVERIFY(reader.verifyAll().ok());
        QVERIFY(reader.extract(QStringLiteral("two"), dir.filePath(QStringLiteral("out.bin"))).ok());
        QCOMPARE(SessionFixture::readAll(dir.filePath(QStringLiteral("out.bin"))), QByteArray("small"));
        QVERIFY(!reader.extract(QStringLiteral("none"), dir.filePath(QStringLiteral("o2"))).ok());
    }

    void aDamagedOrCutOffFileIsRefused()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath(QStringLiteral("a.bin"));
        writeFile(a, QByteArray(100000, 'y'));
        SmplWriter writer;
        QVERIFY(writer.addFile(QStringLiteral("one"), QStringLiteral("a.bin"), a).ok());
        const QString path = dir.filePath(QStringLiteral("x.smpl"));
        QVERIFY(writer.write(path, {}).ok());
        const QByteArray good = SessionFixture::readAll(path);

        const QString cut = dir.filePath(QStringLiteral("cut.smpl"));
        writeFile(cut, good.left(good.size() - 10));
        SmplReader r1;
        QVERIFY(!r1.open(cut).ok());                               // a blob reaches past the end

        QByteArray flipped = good;
        flipped[flipped.size() - 5] = static_cast<char>(flipped.at(flipped.size() - 5) ^ 0x55);
        const QString bad = dir.filePath(QStringLiteral("bad.smpl"));
        writeFile(bad, flipped);
        SmplReader r2;
        QVERIFY(r2.open(bad).ok());
        QVERIFY(!r2.verifyAll().ok());                             // the blob hash does not match

        QByteArray notOurs = good;
        notOurs[0] = 'X';
        const QString foreign = dir.filePath(QStringLiteral("foreign.smpl"));
        writeFile(foreign, notOurs);
        SmplReader r3;
        QVERIFY(!r3.open(foreign).ok());
    }

    // ================================================================ album and playlist between two libraries
    void anAlbumTravelsToAnotherLibraryAndASecondImportAddsNothing()
    {
        SessionFixture from;
        QVERIFY(from.create());
        const AlbumId album = importAlbum(from, QStringLiteral("The Band"), QStringLiteral("First Record"), 3, 100);
        QVERIFY(album.isValid());

        auto tree = from.session->share().describe(ShareKind::Album, album.value);
        QVERIFY(tree.ok());
        QCOMPARE(tree.value().albums.size(), 1);
        QCOMPARE(tree.value().albums.first().tracks.size(), 3);
        QTemporaryDir out;
        const QString file = out.filePath(QStringLiteral("First Record.smpl"));
        QVERIFY(from.session->share().exportTo(file, everything(tree.value())).ok());
        if (qEnvironmentVariableIsSet("SEMPLICE_KEEP_SMPL"))   // for looking at the import page by hand
            QFile::copy(file, qEnvironmentVariable("SEMPLICE_KEEP_SMPL"));

        SessionFixture to;
        QVERIFY(to.create());
        auto first = to.session->share().importFrom(file);
        QVERIFY2(first.ok(), first.ok() ? "" : qPrintable(first.error().symbol() + first.error().devMessage));
        QCOMPARE(first.value().albumsAdded, 1);
        QCOMPARE(first.value().tracksAdded, 3);
        QCOMPARE(to.count(QStringLiteral("track")), 3);
        QCOMPARE(to.count(QStringLiteral("album")), 1);
        QVERIFY(QFileInfo::exists(to.abs(QStringLiteral("Artists/The Band/First Record/01 - First Record 1.mp3"))));
        QCOMPARE(to.verify(), QString());

        auto second = to.session->share().importFrom(file);   // the same file again: everything is skipped
        QVERIFY(second.ok());
        QCOMPARE(second.value().albumsAdded, 0);
        QCOMPARE(second.value().albumsSkipped, 1);
        QCOMPARE(to.count(QStringLiteral("track")), 3);
        QCOMPARE(to.verify(), QString());
    }

    void aPlaylistReusesTracksTheLibraryAlreadyHas()
    {
        SessionFixture from;
        QVERIFY(from.create());
        const AlbumId album = importAlbum(from, QStringLiteral("The Band"), QStringLiteral("First Record"), 3, 100);
        QVERIFY(album.isValid());
        // a playlist of two tracks of that album
        auto ids = AlbumRepo(from.session->database()).tracks(album);
        QVERIFY(ids.ok());
        PlaylistDraft draft;
        draft.name = QStringLiteral("Mix");
        for (int i = 0; i < 2; ++i) {
            TrackInput in;
            in.existing = ids.value().at(i);
            draft.tracks.append(in);
        }
        auto made = from.session->library().createPlaylist(draft);
        QVERIFY(made.ok());
        auto tree = from.session->share().describe(ShareKind::Playlist, made.value().value);
        QVERIFY(tree.ok());
        QTemporaryDir out;
        const QString file = out.filePath(QStringLiteral("Mix.smpl"));
        QVERIFY(from.session->share().exportTo(file, everything(tree.value())).ok());

        // the other library has the album already: the playlist is made from its tracks, nothing is added twice
        SessionFixture to;
        QVERIFY(to.create());
        auto albumTree = from.session->share().describe(ShareKind::Album, album.value);
        const QString albumFile = out.filePath(QStringLiteral("album.smpl"));
        QVERIFY(from.session->share().exportTo(albumFile, everything(albumTree.value())).ok());
        QVERIFY(to.session->share().importFrom(albumFile).ok());
        QCOMPARE(to.count(QStringLiteral("track")), 3);
        auto r = to.session->share().importFrom(file);
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol() + r.error().devMessage));
        QCOMPARE(r.value().playlistsAdded, 1);
        QCOMPARE(r.value().tracksAdded, 0);
        QCOMPARE(r.value().tracksSkipped, 2);
        QCOMPARE(to.count(QStringLiteral("track")), 3);
        QCOMPARE(to.count(QStringLiteral("playlist")), 1);
        QCOMPARE(to.verify(), QString());
    }

    void thePackageIsShownBeforeImportAndOnlyWhatIsTickedComesIn()
    {
        SessionFixture from;
        QVERIFY(from.create());
        const AlbumId album = importAlbum(from, QStringLiteral("The Band"), QStringLiteral("First Record"), 3, 100);
        auto tree = from.session->share().describe(ShareKind::Album, album.value);
        QVERIFY(tree.ok());
        QTemporaryDir out;
        const QString file = out.filePath(QStringLiteral("x.smpl"));
        QVERIFY(from.session->share().exportTo(file, everything(tree.value())).ok());

        SessionFixture to;
        QVERIFY(to.create());
        auto shown = to.session->share().describeFile(file);     // what the import page shows
        QVERIFY(shown.ok());
        QVERIFY(shown.value().importing);
        QCOMPARE(shown.value().albums.size(), 1);
        QCOMPARE(shown.value().albums.first().tracks.size(), 3);
        QVERIFY(!shown.value().albums.first().existing);
        QVERIFY(shown.value().albums.first().tags.contains(QStringLiteral("rock")));
        QCOMPARE(shown.value().artists.first().name, QStringLiteral("The Band"));
        QVERIFY(!shown.value().artists.first().existing);

        // two of the three tracks are ticked, and the new artist is to be hidden
        ImportSelection pick;
        pick.all = false;
        pick.keys = {QStringLiteral("album:0"), QStringLiteral("track:0:0"), QStringLiteral("track:0:2")};
        pick.artistHidden.insert(QStringLiteral("The Band"), true);
        auto done = to.session->share().importFrom(file, {}, pick);
        QVERIFY2(done.ok(), done.ok() ? "" : qPrintable(done.error().symbol() + done.error().devMessage));
        QCOMPARE(to.count(QStringLiteral("track")), 2);
        auto artist = ArtistRepo(to.session->database()).findByName(QStringLiteral("The Band"));
        QVERIFY(artist.ok() && artist.value());
        QVERIFY(artist.value()->userHidden);                    // the status chosen before the import

        auto again = to.session->share().describeFile(file);    // now the library has the artist and the album
        QVERIFY(again.value().albums.first().existing);
        QVERIFY(again.value().artists.first().existing);
        QVERIFY(again.value().artists.first().hidden);          // the library's own status is shown
        QCOMPARE(to.verify(), QString());
    }

    void theStatusChosenWhenSharingTravelsInTheFile()
    {
        SessionFixture from;
        QVERIFY(from.create());
        const AlbumId album = importAlbum(from, QStringLiteral("The Band"), QStringLiteral("First Record"), 2, 100);
        auto tree = from.session->share().describe(ShareKind::Album, album.value);
        ShareSelection s = everything(tree.value());
        s.artistHidden.insert(tree.value().artists.first().id, true);   // "Hide" chosen on the export page
        QTemporaryDir out;
        const QString file = out.filePath(QStringLiteral("x.smpl"));
        QVERIFY(from.session->share().exportTo(file, s).ok());
        SessionFixture to;
        QVERIFY(to.create());
        auto shown = to.session->share().describeFile(file);
        QVERIFY(shown.value().artists.first().hidden);          // the page suggests it
        QVERIFY(to.session->share().importFrom(file).ok());     // taken as it is
        auto artist = ArtistRepo(to.session->database()).findByName(QStringLiteral("The Band"));
        QVERIFY(artist.value()->userHidden);
    }

    void aDamagedPackageChangesNothingInTheLibrary()
    {
        SessionFixture from;
        QVERIFY(from.create());
        const AlbumId album = importAlbum(from, QStringLiteral("The Band"), QStringLiteral("First Record"), 2, 100);
        auto tree = from.session->share().describe(ShareKind::Album, album.value);
        QTemporaryDir out;
        const QString file = out.filePath(QStringLiteral("x.smpl"));
        QVERIFY(from.session->share().exportTo(file, everything(tree.value())).ok());
        QByteArray bytes = SessionFixture::readAll(file);
        bytes[bytes.size() - 20] = static_cast<char>(bytes.at(bytes.size() - 20) ^ 0x33);
        writeFile(file, bytes);

        SessionFixture to;
        QVERIFY(to.create());
        QVERIFY(!to.session->share().importFrom(file).ok());
        QCOMPARE(to.count(QStringLiteral("track")), 0);
        QCOMPARE(to.count(QStringLiteral("album")), 0);
        QCOMPARE(to.verify(), QString());
    }
};

QTEST_GUILESS_MAIN(TstShare)
#include "tst_share.moc"
