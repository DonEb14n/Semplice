#include "TestAudioFiles.h"

#include "core/storage/OwnFiles.h"
#include "core/tags/TagService.h"

#include <QDirIterator>
#include <QTemporaryDir>
#include <QtTest>

#include <fileref.h>
#include <tpropertymap.h>

using namespace semplice;
using namespace semplice::testing;

namespace {

const QByteArray kPng = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==");
const QByteArray kJpeg = QByteArray::fromHex("FFD8FFE000104A46494600010100000100010000FFD9");

class FailingReplace final : public IAtomicReplace {
public:
    Result<void> replaceFile(const QString&, const QString&) override
    {
        return Error::make(ErrorCode::IoError, QStringLiteral("simulated failure"));
    }
};

class RecordingReleaser final : public IFileReleaser {
public:
    Result<void> releaseFile(const QString& path) override
    {
        events << QStringLiteral("release ") + QFileInfo(path).fileName();
        if (onRelease)
            onRelease();
        if (refuse)
            return Error::make(ErrorCode::FileLocked, QStringLiteral("player refuses"));
        return {};
    }
    void fileReady(const QString& path) override { events << QStringLiteral("ready ") + QFileInfo(path).fileName(); }
    bool inUse(const QString&) const override { return playing; }
    bool playing = false;

    QStringList events;
    std::function<void()> onRelease;
    bool refuse = false;
};

TagWrite fullWrite()
{
    TagWrite w;
    w.title = QStringLiteral("Ёлка & 椎名林檎 \U0001F600");
    w.artists = {QStringLiteral("Artist One"), QStringLiteral("Артист Два")};
    w.album = QStringLiteral("Альбом №1");
    w.albumArtists = QStringList{QStringLiteral("Various Artists")};
    w.trackNumber = 3;
    w.trackTotal = 12;
    w.date = ReleaseDate{2020, 5, 17};
    w.genres = {QStringLiteral("Rock"), QStringLiteral("Hip-Hop")};
    w.coverAction = CoverAction::Set;
    w.cover = *CoverImage::fromBytes(kPng);
    return w;
}

// A TagLib file name that stays valid while this object lives.
struct NativeName {
    explicit NativeName(const QString& path)
    {
#ifdef Q_OS_WIN
        wide = path.toStdWString();
#else
        narrow = QFile::encodeName(path);
#endif
    }
    TagLib::FileName get() const
    {
#ifdef Q_OS_WIN
        return TagLib::FileName(wide.c_str());
#else
        return narrow.constData();
#endif
    }
    std::wstring wide;
    QByteArray narrow;
};

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeAll(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

QStringList filesIn(const QString& dir)
{
    return QDir(dir).entryList(QDir::Files | QDir::Hidden, QDir::Name);
}

}  // namespace

class TstTagService : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    PortableAtomicReplace m_replace;

    void addFormats()
    {
        QTest::addColumn<int>("format");
        for (AudioFormat f : allAudioFormats())
            QTest::newRow(qPrintable(nameOf(f))) << static_cast<int>(f);
    }
    QString make(AudioFormat f, const QString& name = QStringLiteral("track")) { return writeTinyAudio(m_dir.path(), name, f); }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void init()
    {
        for (const QString& f : filesIn(m_dir.path()))
            (void)ownfiles::removeFile(m_dir.filePath(f));
    }

    // ================================================================ every format
    void freshFilesReadAsEmpty_data() { addFormats(); }
    void freshFilesReadAsEmpty()
    {
        QFETCH(int, format);
        const AudioFormat f = static_cast<AudioFormat>(format);
        TagService svc(m_replace);
        const auto r = svc.read(make(f));
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().devMessage));
        QVERIFY(r.value().title.isEmpty());
        QVERIFY(r.value().artists.isEmpty());
        QVERIFY(!r.value().hasCover);
        QVERIFY(!r.value().codec.isEmpty());
        if (f != AudioFormat::M4a)  // the minimal M4A has no audio track
            QVERIFY2(r.value().durationMs > 0, qPrintable(QString::number(r.value().durationMs)));
    }

    void roundTripOfEveryField_data() { addFormats(); }
    void roundTripOfEveryField()
    {
        QFETCH(int, format);
        const AudioFormat f = static_cast<AudioFormat>(format);
        const QString path = make(f);
        TagService svc(m_replace);
        const TagWrite w = fullWrite();
        const auto written = svc.write(path, w);
        QVERIFY2(written.ok(), written.ok() ? "" : qPrintable(written.error().devMessage));

        const auto r = svc.read(path);
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().devMessage));
        const TrackTags& t = r.value();
        QCOMPARE(t.title, w.title);
        QCOMPARE(t.artists, w.artists);
        QCOMPARE(t.album, *w.album);
        QCOMPARE(t.albumArtists, *w.albumArtists);
        QCOMPARE(t.trackNumber, 3);
        QCOMPARE(t.trackTotal, 12);
        QCOMPARE(t.date, (ReleaseDate{2020, 5, 17}));
        QCOMPARE(t.genres, w.genres);
        QVERIFY(t.hasCover);
        QVERIFY(t.legacy8bit.isEmpty());  // everything we write is Unicode

        const auto cover = svc.readCover(path);
        QVERIFY(cover.ok() && cover.value().has_value());
        QCOMPARE(cover.value()->data, kPng);
        QCOMPARE(cover.value()->mimeType, QStringLiteral("image/png"));
        QVERIFY(filesIn(m_dir.path()).size() == 1);  // no temporary file left behind
    }

    void partialDates_data() { addFormats(); }
    void partialDates()
    {
        QFETCH(int, format);
        const AudioFormat f = static_cast<AudioFormat>(format);
        TagService svc(m_replace);
        const QString path = make(f);
        TagWrite w;
        w.title = QStringLiteral("t");
        w.artists = {QStringLiteral("a")};
        w.date = ReleaseDate{1999, 0, 0};
        QVERIFY(svc.write(path, w).ok());
        QCOMPARE(svc.read(path).value().date, (ReleaseDate{1999, 0, 0}));
        w.date = ReleaseDate{1999, 11, 0};
        QVERIFY(svc.write(path, w).ok());
        QCOMPARE(svc.read(path).value().date, (ReleaseDate{1999, 11, 0}));
    }

    void albumFieldsAreLeftAloneWhenNotGiven_data() { addFormats(); }
    void albumFieldsAreLeftAloneWhenNotGiven()
    {
        QFETCH(int, format);
        const AudioFormat f = static_cast<AudioFormat>(format);
        TagService svc(m_replace);
        const QString path = make(f);
        QVERIFY(svc.write(path, fullWrite()).ok());

        TagWrite playlistStyle;  // a playlist track: title, artists, genres only (SPEC 6.2)
        playlistStyle.title = QStringLiteral("New title");
        playlistStyle.artists = {QStringLiteral("Someone")};
        playlistStyle.genres = {QStringLiteral("Jazz")};
        QVERIFY(svc.write(path, playlistStyle).ok());

        const TrackTags t = svc.read(path).value();
        QCOMPARE(t.title, QStringLiteral("New title"));
        QCOMPARE(t.artists, QStringList{QStringLiteral("Someone")});
        QCOMPARE(t.genres, QStringList{QStringLiteral("Jazz")});
        QCOMPARE(t.album, *fullWrite().album);       // untouched
        QCOMPARE(t.albumArtists, *fullWrite().albumArtists);
        QCOMPARE(t.trackNumber, 3);
        QCOMPARE(t.date.year, 2020);
        QVERIFY(t.hasCover);                         // cover kept (CoverAction::Keep)
    }

    void unrelatedPropertiesSurviveAWrite_data() { addFormats(); }
    void unrelatedPropertiesSurviveAWrite()
    {
        QFETCH(int, format);
        const AudioFormat f = static_cast<AudioFormat>(format);
        const QString path = make(f);
        {
            const NativeName name(path);
            TagLib::FileRef ref(name.get());
            QVERIFY(!ref.isNull());
            TagLib::PropertyMap map = ref.properties();
            map.replace("COMMENT", TagLib::StringList(TagLib::String("keep me", TagLib::String::UTF8)));
            ref.setProperties(map);
            QVERIFY(ref.save());
        }
        TagService svc(m_replace);
        QVERIFY(svc.write(path, fullWrite()).ok());
        const NativeName name(path);
        TagLib::FileRef check(name.get());
        QVERIFY(!check.isNull());
        TagLib::PropertyMap after = check.properties();
        QVERIFY2(after.contains("COMMENT"), "the comment was dropped");
        QCOMPARE(QString::fromUtf8(after["COMMENT"].front().toCString(true)), QStringLiteral("keep me"));
    }

    void coverCanBeReplacedAndRemoved_data() { addFormats(); }
    void coverCanBeReplacedAndRemoved()
    {
        QFETCH(int, format);
        const AudioFormat f = static_cast<AudioFormat>(format);
        TagService svc(m_replace);
        const QString path = make(f);
        QVERIFY(svc.write(path, fullWrite()).ok());

        TagWrite w;
        w.title = QStringLiteral("t");
        w.artists = {QStringLiteral("a")};
        w.coverAction = CoverAction::Set;
        w.cover = *CoverImage::fromBytes(kJpeg);
        QVERIFY(svc.write(path, w).ok());
        auto cover = svc.readCover(path);
        QVERIFY(cover.ok() && cover.value());
        QCOMPARE(cover.value()->data, kJpeg);
        QCOMPARE(cover.value()->mimeType, QStringLiteral("image/jpeg"));
        QCOMPARE(cover.value()->extension(), QStringLiteral("jpg"));

        w.coverAction = CoverAction::Keep;
        w.title = QStringLiteral("t2");
        QVERIFY(svc.write(path, w).ok());
        QVERIFY(svc.readCover(path).value().has_value());

        w.coverAction = CoverAction::Remove;
        QVERIFY(svc.write(path, w).ok());
        QVERIFY(!svc.readCover(path).value().has_value());
        QVERIFY(!svc.read(path).value().hasCover);
    }

    // ================================================================ MP3 specifics (SPEC 6.2, 6.3)
    void mp3IsWrittenAsId3v24WithUtf8AndWithoutId3v1()
    {
        const QString path = make(AudioFormat::Mp3);
        QByteArray file = readAll(path);
        const QByteArray v23 = buildId3v2(3, {{"TIT2", 0, "Old title"}, {"TPE1", 0, "Old artist"}});
        file = v23 + file + buildId3v1("Old title", "Old artist", "Old album", "1999", 17);
        writeAll(path, file);

        TagService svc(m_replace);
        TagWrite w = fullWrite();
        w.coverAction = CoverAction::Keep;
        QVERIFY(svc.write(path, w).ok());

        const QByteArray after = readAll(path);
        QVERIFY(after.startsWith(QByteArray("ID3\x04", 4)));                      // ID3v2.4
        QVERIFY2(!after.right(128).startsWith("TAG"), "ID3v1 tag must be removed");  // no stale v1 mojibake
        const int title = after.indexOf("TIT2");
        QVERIFY(title > 0);
        QCOMPARE(static_cast<int>(after.at(title + 10)), 3);                       // first payload byte = encoding 3 = UTF-8
        QVERIFY(after.contains(QString::fromUtf8("Артист Два").toUtf8()));        // UTF-8 bytes, not UTF-16
        const QByteArray twoArtists = QByteArray("Artist One") + QByteArray(1, '\0') + QString::fromUtf8("Артист Два").toUtf8();
        QVERIFY2(after.contains(twoArtists), "multi-value TPE1 must be null-separated");
        QCOMPARE(svc.read(path).value().artists, w.artists);
    }

    void aNumericGenreCodeIsReadAsItsNameAndReplacedOnWrite()
    {
        // "7" (and "(7)") are the old ID3v1 codes for Hip-Hop; TagLib would keep the code when the same genre is "written"
        const QString path = make(AudioFormat::Mp3);
        QByteArray file = readAll(path);
        writeAll(path, buildId3v2(4, {{"TIT2", 3, "Song"}, {"TCON", 3, "7"}}) + file);

        TagService svc(m_replace);
        QCOMPARE(svc.read(path).value().genres, QStringList{QStringLiteral("Hip-Hop")});

        TagWrite w;
        w.title = QStringLiteral("Song");
        w.genres = {QStringLiteral("Hip-Hop")};
        QVERIFY2(svc.write(path, w).ok(), "the verification must accept a file that stored the genre as a code");
        QVERIFY(!readAll(path).contains(QByteArray("TCON") + QByteArray(6, '\0') + QByteArray("\x03" "7", 2)));
        QCOMPARE(svc.read(path).value().genres, QStringList{QStringLiteral("Hip-Hop")});
    }

    void mp3ReportsWhichFieldsAreLegacy8Bit()
    {
        TagService svc(m_replace);
        const QByteArray audio = tinyAudio(AudioFormat::Mp3);

        // ISO-8859-1 declared ID3v2.3 frames: legacy
        QString path = m_dir.filePath(QStringLiteral("latin1.mp3"));
        writeAll(path, buildId3v2(3, {{"TIT2", 0, "Title"}, {"TPE1", 0, "Artist"}, {"TALB", 0, "Album"}, {"TCON", 0, "Rock"}}) + audio);
        TrackTags t = svc.read(path).value();
        QVERIFY(t.legacy8bit.contains(tagfield::Title));
        QVERIFY(t.legacy8bit.contains(tagfield::Artists));
        QVERIFY(t.legacy8bit.contains(tagfield::Album));
        QVERIFY(t.legacy8bit.contains(tagfield::Genres));
        QCOMPARE(t.title, QStringLiteral("Title"));

        // UTF-16 and UTF-8 declared frames: trusted
        path = m_dir.filePath(QStringLiteral("utf16.mp3"));
        const QByteArray utf16 = QByteArray::fromHex("FFFE") + QByteArray::fromRawData(reinterpret_cast<const char*>(u"Title"), 10);
        writeAll(path, buildId3v2(3, {{"TIT2", 1, utf16}}) + audio);
        t = svc.read(path).value();
        QVERIFY(!t.legacy8bit.contains(tagfield::Title));
        QCOMPARE(t.title, QStringLiteral("Title"));

        path = m_dir.filePath(QStringLiteral("utf8.mp3"));
        writeAll(path, buildId3v2(4, {{"TIT2", 3, QString::fromUtf8("Привет").toUtf8()}}) + audio);
        t = svc.read(path).value();
        QVERIFY(t.legacy8bit.isEmpty());
        QCOMPARE(t.title, QStringLiteral("Привет"));

        // ID3v1 only: legacy, and the genre byte becomes a name
        path = m_dir.filePath(QStringLiteral("v1only.mp3"));
        writeAll(path, audio + buildId3v1("V1 title", "V1 artist", "V1 album", "1998", 17));
        t = svc.read(path).value();
        QVERIFY(t.legacy8bit.contains(tagfield::Title));
        QVERIFY(t.legacy8bit.contains(tagfield::Artists));
        QCOMPARE(t.title, QStringLiteral("V1 title"));
        QCOMPARE(t.genres, QStringList{QStringLiteral("Rock")});
        QCOMPARE(t.date.year, 1998);
    }

    void mp3LatinBytesAreReadAsTheyAreSoTheFixerCanSeeThem()
    {
        // "Мумий Тролль" stored as CP1251 bytes inside an ISO-8859-1 frame: reads as mojibake text.
        const QByteArray cp1251 = QByteArray::fromHex("CCF3ECE8E920D2F0EEEBEBFC");
        const QString path = m_dir.filePath(QStringLiteral("mojibake.mp3"));
        writeAll(path, buildId3v2(3, {{"TPE1", 0, cp1251}}) + tinyAudio(AudioFormat::Mp3));
        const TrackTags t = TagService(m_replace).read(path).value();
        QCOMPARE(t.artists.size(), 1);
        QCOMPARE(t.artists.first(), QString::fromLatin1(cp1251));
        QVERIFY(t.legacy8bit.contains(tagfield::Artists));
    }

    void id3v23DateAndTrackAreUnderstood()
    {
        const QString path = m_dir.filePath(QStringLiteral("v23.mp3"));
        writeAll(path, buildId3v2(3, {{"TIT2", 0, "T"}, {"TYER", 0, "2004"}, {"TDAT", 0, "1703"}, {"TRCK", 0, "7/15"}}) +
                           tinyAudio(AudioFormat::Mp3));
        const TrackTags t = TagService(m_replace).read(path).value();
        QCOMPARE(t.date, (ReleaseDate{2004, 3, 17}));  // TDAT is DDMM
        QCOMPARE(t.trackNumber, 7);
        QCOMPARE(t.trackTotal, 15);
    }

    // ================================================================ errors and safety
    void unsupportedMissingAndBrokenFilesAreReportedClearly()
    {
        TagService svc(m_replace);
        writeAll(m_dir.filePath(QStringLiteral("notes.txt")), "hello");
        QCOMPARE(svc.read(m_dir.filePath(QStringLiteral("notes.txt"))).error().code, ErrorCode::UnsupportedFile);
        QCOMPARE(svc.read(m_dir.filePath(QStringLiteral("missing.mp3"))).error().code, ErrorCode::NotFound);
        writeAll(m_dir.filePath(QStringLiteral("broken.flac")), "this is not a flac file at all");
        QCOMPARE(svc.read(m_dir.filePath(QStringLiteral("broken.flac"))).error().code, ErrorCode::UnsupportedFile);
        QCOMPARE(svc.write(m_dir.filePath(QStringLiteral("notes.txt")), fullWrite()).error().code, ErrorCode::UnsupportedFile);
        TagWrite noTitle = fullWrite();
        noTitle.title = QStringLiteral("  ");
        QCOMPARE(svc.write(make(AudioFormat::Mp3), noTitle).error().code, ErrorCode::NameEmpty);
        TagWrite badCover = fullWrite();
        badCover.cover.data = QByteArray("not an image");
        QCOMPARE(svc.write(make(AudioFormat::Flac), badCover).error().code, ErrorCode::InvalidArgument);
    }

    void aReadOnlyFileCanStillBeRead()
    {
        const QString path = make(AudioFormat::Flac);
        QFile f(path);
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner));
        const auto r = TagService(m_replace).read(path);
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(r.ok());
    }

    void aFailedReplaceLeavesTheOriginalByteForByteUntouched_data() { addFormats(); }
    void aFailedReplaceLeavesTheOriginalByteForByteUntouched()
    {
        QFETCH(int, format);
        const QString path = make(static_cast<AudioFormat>(format));
        const QByteArray before = readAll(path);
        FailingReplace failing;
        TagService svc(failing);
        const auto r = svc.write(path, fullWrite());
        QVERIFY(!r.ok());
        QCOMPARE(readAll(path), before);
        QCOMPARE(filesIn(m_dir.path()).size(), 1);  // the temporary copy is gone
    }

    void theReleaserIsAskedFirstAndToldAfterwardsEvenOnFailure()
    {
        const QString path = make(AudioFormat::Flac, QStringLiteral("song"));
        RecordingReleaser releaser;
        TagService svc(m_replace, &releaser);
        QVERIFY(svc.write(path, fullWrite()).ok());
        QCOMPARE(releaser.events, (QStringList{QStringLiteral("release song.flac"), QStringLiteral("ready song.flac")}));

        releaser.events.clear();
        FailingReplace failing;
        TagService failingSvc(failing, &releaser);
        QVERIFY(!failingSvc.write(path, fullWrite()).ok());
        QCOMPARE(releaser.events, (QStringList{QStringLiteral("release song.flac"), QStringLiteral("ready song.flac")}));

        // The player refuses: nothing happens at all.
        releaser.events.clear();
        releaser.refuse = true;
        const QByteArray before = readAll(path);
        QVERIFY(!svc.write(path, fullWrite()).ok());
        QCOMPARE(readAll(path), before);
        QCOMPARE(releaser.events, QStringList{QStringLiteral("release song.flac")});
    }

    void aFileThatIsPlayingIsNotWrittenNowAndNotTouchedAtAll()
    {
        const QString path = make(AudioFormat::Flac, QStringLiteral("song"));
        RecordingReleaser releaser;
        releaser.playing = true;
        TagService svc(m_replace, &releaser);
        const QByteArray before = readAll(path);
        const auto r = svc.write(path, fullWrite());
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::FileLocked);  // the caller keeps the tags marked "out of sync" and tries again later
        QCOMPARE(readAll(path), before);
        QVERIFY(releaser.events.isEmpty());  // the player was not stopped, nothing was copied
        QCOMPARE(filesIn(m_dir.path()).size(), 1);
        releaser.playing = false;
        QVERIFY(svc.write(path, fullWrite()).ok());  // once the track is over it is written
    }

    void aFileHeldOpenByAnotherProgramIsReportedAndThenWrittenAfterRelease()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows keeps files open exclusively; other systems allow the replace");
#else
        const QString path = make(AudioFormat::Mp3, QStringLiteral("playing"));
        const QByteArray before = readAll(path);
        auto holder = std::make_unique<QFile>(path);
        QVERIFY(holder->open(QIODevice::ReadOnly));  // "the player": Qt opens without FILE_SHARE_DELETE

        TagService svc(m_replace);
        const auto locked = svc.write(path, fullWrite());
        QVERIFY(!locked.ok());
        QCOMPARE(locked.error().code, ErrorCode::FileLocked);
        QCOMPARE(readAll(path), before);               // untouched
        QCOMPARE(filesIn(m_dir.path()).size(), 1);     // temp copy cleaned up

        // With the handshake the player lets go first.
        RecordingReleaser releaser;
        releaser.onRelease = [&] { holder.reset(); };
        TagService polite(m_replace, &releaser);
        const auto ok = polite.write(path, fullWrite());
        QVERIFY2(ok.ok(), ok.ok() ? "" : qPrintable(ok.error().devMessage));
        QCOMPARE(svc.read(path).value().title, fullWrite().title);
#endif
    }

    void staleTemporaryFilesAreCleanedUpAtStart()
    {
        const QString root = m_dir.filePath(QStringLiteral("lib"));
        QDir().mkpath(root + QStringLiteral("/Artists/A/Album"));
        const QString album = root + QStringLiteral("/Artists/A/Album");
        writeAll(album + QStringLiteral("/01 - a.mp3.semplice-tagtmp-1234abcd"), "half written");
        // crash between "set the original aside" and "move the new file in": the original must come back
        writeAll(album + QStringLiteral("/02 - b.mp3.semplice-old-1234abcd"), "the real file");
        // finished replace whose backup removal was interrupted: backup is just garbage
        writeAll(album + QStringLiteral("/03 - c.mp3"), "new");
        writeAll(album + QStringLiteral("/03 - c.mp3.semplice-old-1234abcd"), "old");
        writeAll(album + QStringLiteral("/04 - d.mp3"), "innocent bystander");

        QCOMPARE(TagService::removeStaleTempFiles(root), 3);
        QCOMPARE(readAll(album + QStringLiteral("/02 - b.mp3")), QByteArray("the real file"));
        QCOMPARE(readAll(album + QStringLiteral("/03 - c.mp3")), QByteArray("new"));
        QCOMPARE(readAll(album + QStringLiteral("/04 - d.mp3")), QByteArray("innocent bystander"));
        QCOMPARE(QDir(album).entryList(QDir::Files).size(), 3);
        QCOMPARE(TagService::removeStaleTempFiles(root), 0);
    }

    void supportedExtensions()
    {
        for (const char* ext : {"mp3", "MP3", "flac", "ogg", "oga", "m4a", "wav", "wma"})
            QVERIFY2(TagService::isSupportedExtension(QString::fromLatin1(ext)), ext);
        for (const char* ext : {"txt", "jpg", "aac", "opus", ""})
            QVERIFY2(!TagService::isSupportedExtension(QString::fromLatin1(ext)), ext);
    }
};

QTEST_GUILESS_MAIN(TstTagService)
#include "tst_tag_service.moc"
