#include "TestAudioFiles.h"

#include "core/tags/CoverDiscovery.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace semplice;
using namespace semplice::testing;

namespace {

const QByteArray kPng = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==");
const QByteArray kJpeg = QByteArray::fromHex("FFD8FFE000104A46494600010100000100010000FFD9");

void put(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

}  // namespace

class TstCoverDiscovery : public QObject {
    Q_OBJECT

private slots:
    // ================================================================ cover files in a folder
    void coverBeatsAlbumCoverAndOtherNamesAreIgnored()
    {
        QTemporaryDir d;
        put(d.filePath(QStringLiteral("folder.jpg")), kJpeg);
        put(d.filePath(QStringLiteral("front.png")), kPng);
        QVERIFY(CoverDiscovery::findInFolder(d.path()).isEmpty());  // only cover.* and album_cover.* count

        put(d.filePath(QStringLiteral("album_cover.jpg")), kJpeg);
        QCOMPARE(QFileInfo(CoverDiscovery::findInFolder(d.path())).fileName(), QStringLiteral("album_cover.jpg"));

        put(d.filePath(QStringLiteral("cover.png")), kPng);  // "cover.*" comes first (UI document)
        QCOMPARE(QFileInfo(CoverDiscovery::findInFolder(d.path())).fileName(), QStringLiteral("cover.png"));

        put(d.filePath(QStringLiteral("cover.jpg")), kJpeg);  // jpg before png
        QCOMPARE(QFileInfo(CoverDiscovery::findInFolder(d.path())).fileName(), QStringLiteral("cover.jpg"));
    }

    void namesAreCaseInsensitiveAndTheContentMustBeAnImage()
    {
        QTemporaryDir d;
        put(d.filePath(QStringLiteral("Cover.JPG")), QByteArray("this is not a picture"));  // renamed junk
        QVERIFY(CoverDiscovery::findInFolder(d.path()).isEmpty());
        put(d.filePath(QStringLiteral("COVER.JPEG")), kJpeg);
        QCOMPARE(QFileInfo(CoverDiscovery::findInFolder(d.path())).fileName(), QStringLiteral("COVER.JPEG"));
        put(d.filePath(QStringLiteral("sub/cover.png")), kPng);  // sub folders are not searched
        QCOMPARE(QFileInfo(CoverDiscovery::findInFolder(d.path())).dir().path(), d.path());
        QVERIFY(CoverDiscovery::findInFolder(d.filePath(QStringLiteral("does-not-exist"))).isEmpty());
    }

    void imagesAreRecognisedByContentAndSize()
    {
        QTemporaryDir d;
        put(d.filePath(QStringLiteral("a.bin")), kPng);  // wrong name, right content
        const auto img = CoverDiscovery::loadImageFile(d.filePath(QStringLiteral("a.bin")));
        QVERIFY(img.has_value());
        QCOMPARE(img->mimeType, QStringLiteral("image/png"));
        QCOMPARE(img->extension(), QStringLiteral("png"));
        put(d.filePath(QStringLiteral("b.jpg")), QByteArray("GIF89a...."));
        QVERIFY(!CoverDiscovery::loadImageFile(d.filePath(QStringLiteral("b.jpg"))).has_value());
        QVERIFY(!CoverDiscovery::loadImageFile(d.filePath(QStringLiteral("missing.jpg"))).has_value());
    }

    // ================================================================ names inside the library
    void storedNamesAreAlwaysFixed()
    {
        const CoverImage png = *CoverImage::fromBytes(kPng);
        const CoverImage jpg = *CoverImage::fromBytes(kJpeg);
        QCOMPARE(CoverDiscovery::storedName(CoverOwner::Album, jpg), QStringLiteral("album_cover.jpg"));
        QCOMPARE(CoverDiscovery::storedName(CoverOwner::Album, png), QStringLiteral("album_cover.png"));
        QCOMPARE(CoverDiscovery::storedName(CoverOwner::Artist, jpg), QStringLiteral("artist_cover.jpg"));
        QCOMPARE(CoverDiscovery::storedName(CoverOwner::Playlist, png), QStringLiteral("playlist_cover.png"));
    }

    // ================================================================ priority
    void userChoiceBeatsFolderFileBeatsEmbeddedPicture()
    {
        QTemporaryDir d;
        PortableAtomicReplace replace;
        TagService tags(replace);
        const QString folder = d.filePath(QStringLiteral("album"));
        const QString track = writeTinyAudio(folder, QStringLiteral("01"), AudioFormat::Flac);
        TagWrite w;
        w.title = QStringLiteral("t");
        w.artists = {QStringLiteral("a")};
        w.coverAction = CoverAction::Set;
        w.cover = *CoverImage::fromBytes(kPng);
        QVERIFY(tags.write(track, w).ok());
        put(folder + QStringLiteral("/cover.jpg"), kJpeg);
        put(d.filePath(QStringLiteral("mine.jpg")), kJpeg + QByteArray("user"));

        auto r = CoverDiscovery::resolve(d.filePath(QStringLiteral("mine.jpg")), folder, tags, {track});
        QVERIFY(r.ok());
        QCOMPARE(r.value().kind, CoverSource::Kind::UserChosen);
        QCOMPARE(r.value().image->data, kJpeg + QByteArray("user"));

        r = CoverDiscovery::resolve({}, folder, tags, {track});
        QCOMPARE(r.value().kind, CoverSource::Kind::FolderFile);
        QCOMPARE(QFileInfo(r.value().path).fileName(), QStringLiteral("cover.jpg"));

        QFile::remove(folder + QStringLiteral("/cover.jpg"));  // test cleanup
        r = CoverDiscovery::resolve({}, folder, tags, {track});
        QCOMPARE(r.value().kind, CoverSource::Kind::Embedded);
        QCOMPARE(r.value().image->data, kPng);
        QVERIFY(r.value().path.isEmpty());
    }

    void onlyTheFirstTracksPictureCounts()
    {
        QTemporaryDir d;
        PortableAtomicReplace replace;
        TagService tags(replace);
        const QString first = writeTinyAudio(d.path(), QStringLiteral("01"), AudioFormat::Mp3);
        const QString second = writeTinyAudio(d.path(), QStringLiteral("02"), AudioFormat::Mp3);
        TagWrite w;
        w.title = QStringLiteral("t");
        w.artists = {QStringLiteral("a")};
        w.coverAction = CoverAction::Set;
        w.cover = *CoverImage::fromBytes(kJpeg);
        QVERIFY(tags.write(second, w).ok());  // the SECOND track has the picture
        auto r = CoverDiscovery::resolve({}, d.path(), tags, {first, second});
        QVERIFY(r.ok());
        QCOMPARE(r.value().kind, CoverSource::Kind::None);  // placeholder, per SPEC 4.1
        QVERIFY(!r.value().image.has_value());
    }

    void aUnusableUserChoiceIsAnError()
    {
        QTemporaryDir d;
        PortableAtomicReplace replace;
        TagService tags(replace);
        put(d.filePath(QStringLiteral("x.jpg")), QByteArray("not an image"));
        const auto r = CoverDiscovery::resolve(d.filePath(QStringLiteral("x.jpg")), d.path(), tags, {});
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::InvalidArgument);
        QCOMPARE(CoverDiscovery::resolve({}, {}, tags, {}).value().kind, CoverSource::Kind::None);
    }

    // ================================================================ writing
    void coverFilesAreWrittenOnceAndNeverOverwritten()
    {
        QTemporaryDir d;
        const CoverImage img = *CoverImage::fromBytes(kPng);
        const QString target = d.filePath(QStringLiteral("album_cover.png"));
        QVERIFY(CoverDiscovery::writeFile(img, target).ok());
        QCOMPARE(CoverDiscovery::loadImageFile(target)->data, kPng);
        const auto again = CoverDiscovery::writeFile(*CoverImage::fromBytes(kJpeg), target);
        QVERIFY(!again.ok());
        QCOMPARE(again.error().code, ErrorCode::AlreadyExists);
        QCOMPARE(CoverDiscovery::loadImageFile(target)->data, kPng);  // still the first one
        QCOMPARE(QDir(d.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TstCoverDiscovery)
#include "tst_cover_discovery.moc"
