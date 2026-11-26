#include "core/tags/TagService.h"

#include "core/storage/OwnFiles.h"
#include "core/util/Logger.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

#include <asfattribute.h>
#include <asffile.h>
#include <asfpicture.h>
#include <asftag.h>
#include <attachedpictureframe.h>
#include <flacfile.h>
#include <flacpicture.h>
#include <id3v1genres.h>
#include <id3v1tag.h>
#include <id3v2framefactory.h>
#include <id3v2tag.h>
#include <mp4coverart.h>
#include <mp4file.h>
#include <mp4item.h>
#include <mp4properties.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <textidentificationframe.h>
#include <tdebuglistener.h>
#include <tfilestream.h>
#include <tpropertymap.h>
#include <vorbisfile.h>
#include <wavfile.h>
#include <xiphcomment.h>

namespace semplice {

namespace TL = TagLib;

#define SEMPLICE_TRY(expr)                      \
    do {                                        \
        auto semplice_try_result = (expr);      \
        if (!semplice_try_result.ok())          \
            return semplice_try_result.error(); \
    } while (false)

namespace {

// TagLib prints its diagnostics ("MP4: No audio tracks", ...) to stderr by default. Send them to our log instead.
class TagLibLogListener final : public TL::DebugListener {
public:
    void printMessage(const TL::String& msg) override
    {
        QString text = QString::fromUtf8(msg.toCString(true)).trimmed();
        if (!text.isEmpty())
            SLOG_DEBUG(QStringLiteral("TagLib: ") + text);
    }
};

void installTagLibLogging()
{
    static TagLibLogListener listener;
    static const bool once = (TL::setDebugListener(&listener), true);
    Q_UNUSED(once);
}

enum class Fmt { Unknown, Mp3, Flac, Ogg, M4a, Wav, Wma };

Fmt formatOf(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    if (s == QLatin1String("mp3")) return Fmt::Mp3;
    if (s == QLatin1String("flac")) return Fmt::Flac;
    if (s == QLatin1String("ogg") || s == QLatin1String("oga")) return Fmt::Ogg;
    if (s == QLatin1String("m4a") || s == QLatin1String("m4b") || s == QLatin1String("mp4")) return Fmt::M4a;
    if (s == QLatin1String("wav")) return Fmt::Wav;
    if (s == QLatin1String("wma")) return Fmt::Wma;
    return Fmt::Unknown;
}

// A TagLib file name that stays valid as long as this object lives.
struct TlPath {
    explicit TlPath(const QString& path)
    {
#ifdef Q_OS_WIN
        wide = path.toStdWString();
#else
        narrow = QFile::encodeName(path);
#endif
    }
    TL::FileName name() const
    {
#ifdef Q_OS_WIN
        return TL::FileName(wide.c_str());
#else
        return narrow.constData();
#endif
    }
    std::wstring wide;
    QByteArray narrow;
};

QString q(const TL::String& s) { return QString::fromUtf8(s.toCString(true)); }
QStringList q(const TL::StringList& l)
{
    QStringList out;
    for (const TL::String& s : l)
        out << q(s);
    return out;
}
TL::String t(const QString& s) { return TL::String(s.toUtf8().constData(), TL::String::UTF8); }
TL::StringList tl(const QStringList& l)
{
    TL::StringList out;
    for (const QString& s : l)
        out.append(t(s));
    return out;
}
QByteArray toQ(const TL::ByteVector& v) { return QByteArray(v.data(), static_cast<qsizetype>(v.size())); }
TL::ByteVector toTL(const QByteArray& b) { return TL::ByteVector(b.constData(), static_cast<unsigned int>(b.size())); }

// ---- parsing helpers -----------------------------------------------------------------------------------

ReleaseDate parseDate(const QString& text)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*(\d{4})(?:[-/.](\d{1,2}))?(?:[-/.](\d{1,2}))?)"));
    const auto m = re.match(text);
    ReleaseDate d;
    if (!m.hasMatch())
        return d;
    d.year = m.captured(1).toInt();
    const int month = m.captured(2).toInt();
    const int day = m.captured(3).toInt();
    if (month >= 1 && month <= 12) {
        d.month = month;
        if (day >= 1 && day <= 31)
            d.day = day;
    }
    return d;
}

QString formatDate(const ReleaseDate& d)
{
    if (d.year <= 0)
        return {};
    if (d.month <= 0)
        return QStringLiteral("%1").arg(d.year, 4, 10, QLatin1Char('0'));
    if (d.day <= 0)
        return QStringLiteral("%1-%2").arg(d.year, 4, 10, QLatin1Char('0')).arg(d.month, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1-%2-%3")
        .arg(d.year, 4, 10, QLatin1Char('0'))
        .arg(d.month, 2, 10, QLatin1Char('0'))
        .arg(d.day, 2, 10, QLatin1Char('0'));
}

void parseTrack(const QString& text, TrackTags& tags)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*(\d+)\s*(?:/\s*(\d+))?)"));
    const auto m = re.match(text);
    if (!m.hasMatch())
        return;
    tags.trackNumber = m.captured(1).toInt();
    tags.trackTotal = m.captured(2).toInt();
}

QStringList nonEmpty(QStringList list)
{
    list.removeAll(QString());
    return list;
}

// ---- reading -------------------------------------------------------------------------------------------

void fillFromProperties(TrackTags& tags, TL::PropertyMap map)
{
    auto all = [&](const char* key) { return map.contains(key) ? nonEmpty(q(map[key])) : QStringList(); };
    auto first = [&](const char* key) {
        const QStringList v = all(key);
        return v.isEmpty() ? QString() : v.first();
    };
    tags.title = first("TITLE");
    tags.artists = all("ARTIST");
    tags.album = first("ALBUM");
    tags.albumArtists = all("ALBUMARTIST");
    tags.genres = all("GENRE");
    parseTrack(first("TRACKNUMBER"), tags);
    QString date = first("DATE");
    if (date.isEmpty())
        date = first("YEAR");
    tags.date = parseDate(date);
}

void fillAudio(TrackTags& tags, TL::AudioProperties* props)
{
    if (!props)
        return;
    tags.durationMs = props->lengthInMilliseconds();
    tags.bitrateKbps = props->bitrate();
}

std::optional<CoverImage> bestApic(TL::ID3v2::Tag* tag)
{
    if (!tag)
        return std::nullopt;
    std::optional<CoverImage> fallback;
    for (TL::ID3v2::Frame* frame : tag->frameList("APIC")) {
        auto* pic = dynamic_cast<TL::ID3v2::AttachedPictureFrame*>(frame);
        if (!pic)
            continue;
        const auto image = CoverImage::fromBytes(toQ(pic->picture()));
        if (!image)
            continue;
        if (pic->type() == TL::ID3v2::AttachedPictureFrame::FrontCover)
            return image;
        if (!fallback)
            fallback = image;
    }
    return fallback;
}

template <class PictureList>
std::optional<CoverImage> bestFlacPicture(const PictureList& list)
{
    std::optional<CoverImage> fallback;
    for (const TL::FLAC::Picture* pic : list) {
        const auto image = CoverImage::fromBytes(toQ(pic->data()));
        if (!image)
            continue;
        if (pic->type() == TL::FLAC::Picture::FrontCover)
            return image;
        if (!fallback)
            fallback = image;
    }
    return fallback;
}

std::optional<CoverImage> coverOf(TL::MPEG::File& f) { return bestApic(f.ID3v2Tag(false)); }
std::optional<CoverImage> coverOf(TL::RIFF::WAV::File& f) { return f.hasID3v2Tag() ? bestApic(f.ID3v2Tag()) : std::nullopt; }
std::optional<CoverImage> coverOf(TL::FLAC::File& f) { return bestFlacPicture(f.pictureList()); }
std::optional<CoverImage> coverOf(TL::Ogg::Vorbis::File& f) { return f.tag() ? bestFlacPicture(f.tag()->pictureList()) : std::nullopt; }

std::optional<CoverImage> coverOf(TL::MP4::File& f)
{
    if (!f.tag() || !f.tag()->contains("covr"))
        return std::nullopt;
    for (const TL::MP4::CoverArt& art : f.tag()->item("covr").toCoverArtList()) {
        if (const auto image = CoverImage::fromBytes(toQ(art.data())))
            return image;
    }
    return std::nullopt;
}

std::optional<CoverImage> coverOf(TL::ASF::File& f)
{
    if (!f.tag())
        return std::nullopt;
    std::optional<CoverImage> fallback;
    for (const TL::ASF::Attribute& attr : f.tag()->attribute("WM/Picture")) {
        const TL::ASF::Picture pic = attr.toPicture();
        const auto image = CoverImage::fromBytes(toQ(pic.picture()));
        if (!image)
            continue;
        if (pic.type() == TL::ASF::Picture::FrontCover)
            return image;
        if (!fallback)
            fallback = image;
    }
    return fallback;
}

Result<TrackTags> readMp3(TL::MPEG::File& f)
{
    TrackTags tags;
    TL::ID3v2::Tag* v2 = f.ID3v2Tag(false);
    TL::ID3v1::Tag* v1 = f.ID3v1Tag(false);

    auto frame = [&](const char* id) -> TL::ID3v2::TextIdentificationFrame* {
        if (!v2 || v2->frameList(id).isEmpty())
            return nullptr;
        return dynamic_cast<TL::ID3v2::TextIdentificationFrame*>(v2->frameList(id).front());
    };
    // Returns the values of a text frame and flags the field when the frame is declared ISO-8859-1.
    auto values = [&](const char* id, const QString& field) -> QStringList {
        auto* fr = frame(id);
        if (!fr)
            return {};
        if (fr->textEncoding() == TL::String::Latin1)
            tags.legacy8bit.insert(field);
        return nonEmpty(q(fr->fieldList()));
    };

    QStringList list = values("TIT2", tagfield::Title);
    if (!list.isEmpty())
        tags.title = list.first();
    tags.artists = values("TPE1", tagfield::Artists);
    list = values("TALB", tagfield::Album);
    if (!list.isEmpty())
        tags.album = list.first();
    tags.albumArtists = values("TPE2", tagfield::AlbumArtists);
    tags.genres = values("TCON", tagfield::Genres);
    for (QString& g : tags.genres) {  // "7" or "(7)": the old numeric codes
        static const QRegularExpression code(QStringLiteral("^\\(?(\\d{1,3})\\)?$"));
        const auto m = code.match(g);
        if (m.hasMatch()) {
            const TL::String name = TL::ID3v1::genre(m.captured(1).toInt());
            if (!name.isEmpty())
                g = q(name);
        }
    }
    list = values("TRCK", QStringLiteral("(track)"));
    if (!list.isEmpty())
        parseTrack(list.first(), tags);

    list = values("TDRC", QStringLiteral("(date)"));
    if (list.isEmpty()) {  // ID3v2.3 style: TYER (+ TDAT as DDMM)
        const QStringList year = values("TYER", QStringLiteral("(date)"));
        if (!year.isEmpty()) {
            tags.date = parseDate(year.first());
            const QStringList dm = values("TDAT", QStringLiteral("(date)"));
            if (!dm.isEmpty() && dm.first().size() == 4 && tags.date.year > 0) {
                const int day = dm.first().left(2).toInt();
                const int month = dm.first().mid(2, 2).toInt();
                if (month >= 1 && month <= 12) {
                    tags.date.month = month;
                    tags.date.day = (day >= 1 && day <= 31) ? day : 0;
                }
            }
        }
    } else {
        tags.date = parseDate(list.first());
    }

    // ID3v1 fills what ID3v2 does not say. Its text is always legacy 8-bit.
    if (v1) {
        if (tags.title.isEmpty() && !v1->title().isEmpty()) {
            tags.title = q(v1->title());
            tags.legacy8bit.insert(tagfield::Title);
        }
        if (tags.artists.isEmpty() && !v1->artist().isEmpty()) {
            tags.artists = {q(v1->artist())};
            tags.legacy8bit.insert(tagfield::Artists);
        }
        if (tags.album.isEmpty() && !v1->album().isEmpty()) {
            tags.album = q(v1->album());
            tags.legacy8bit.insert(tagfield::Album);
        }
        if (tags.genres.isEmpty() && !v1->genre().isEmpty())
            tags.genres = {q(v1->genre())};
        if (tags.trackNumber == 0 && v1->track() > 0)
            tags.trackNumber = static_cast<int>(v1->track());
        if (tags.date.year == 0 && v1->year() > 0)
            tags.date.year = static_cast<int>(v1->year());
    }
    tags.codec = QStringLiteral("mp3");
    fillAudio(tags, f.audioProperties());
    tags.hasCover = coverOf(f).has_value();
    return tags;
}

template <class F>
Result<TrackTags> readViaProperties(F& f, const QString& codec)
{
    TrackTags tags;
    fillFromProperties(tags, f.properties());
    tags.codec = codec;
    fillAudio(tags, f.audioProperties());
    tags.hasCover = coverOf(f).has_value();
    return tags;
}

Result<TrackTags> readByFormat(const QString& path, Fmt fmt)
{
    if (fmt == Fmt::Unknown)
        return Error::make(ErrorCode::UnsupportedFile, QStringLiteral("unsupported file type: ") + path);
    if (!QFileInfo(path).isFile())
        return Error::make(ErrorCode::NotFound, path);

    const TlPath p(path);
    TL::FileStream stream(p.name(), true);  // read-only: originals are never opened for writing
    if (!stream.isOpen())
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot open ") + path);
    const auto bad = [&] { return Error::make(ErrorCode::UnsupportedFile, QStringLiteral("not a valid audio file: ") + path); };

    switch (fmt) {
    case Fmt::Mp3: {
        TL::MPEG::File f(&stream, true);
        return f.isValid() ? readMp3(f) : Result<TrackTags>(bad());
    }
    case Fmt::Flac: {
        TL::FLAC::File f(&stream, true);
        return f.isValid() ? readViaProperties(f, QStringLiteral("flac")) : Result<TrackTags>(bad());
    }
    case Fmt::Ogg: {
        TL::Ogg::Vorbis::File f(&stream, true);
        return f.isValid() ? readViaProperties(f, QStringLiteral("vorbis")) : Result<TrackTags>(bad());
    }
    case Fmt::M4a: {
        TL::MP4::File f(&stream, true);
        if (!f.isValid())
            return bad();
        auto tags = readViaProperties(f, QStringLiteral("aac"));
        if (tags.ok()) {
            if (auto* props = dynamic_cast<TL::MP4::Properties*>(f.audioProperties()); props && props->codec() == TL::MP4::Properties::ALAC)
                tags.value().codec = QStringLiteral("alac");
        }
        return tags;
    }
    case Fmt::Wav: {
        TL::RIFF::WAV::File f(&stream, true);
        return f.isValid() ? readViaProperties(f, QStringLiteral("pcm")) : Result<TrackTags>(bad());
    }
    case Fmt::Wma: {
        TL::ASF::File f(&stream, true);
        if (!f.isValid())
            return bad();
        auto tags = readViaProperties(f, QStringLiteral("wma"));
        if (tags.ok()) {
            // The ASF "Author" field is a single string: several artists are stored separated by ";".
            QStringList split;
            for (const QString& a : std::as_const(tags.value().artists)) {
                for (const QString& part : a.split(QLatin1Char(';'), Qt::SkipEmptyParts))
                    split << part.trimmed();
            }
            tags.value().artists = split;
        }
        return tags;
    }
    case Fmt::Unknown:
        break;
    }
    return bad();
}

// ---- writing --------------------------------------------------------------------------------------------

void setList(TL::PropertyMap& map, const char* key, const QStringList& values)
{
    const QStringList clean = nonEmpty(values);
    if (clean.isEmpty())
        map.erase(key);
    else
        map.replace(key, tl(clean));
}

void applyProperties(TL::PropertyMap& map, const TagWrite& w)
{
    setList(map, "TITLE", {w.title});
    setList(map, "ARTIST", w.artists);
    if (w.album)
        setList(map, "ALBUM", {*w.album});
    if (w.albumArtists)
        setList(map, "ALBUMARTIST", *w.albumArtists);
    if (w.trackNumber) {
        QString track = *w.trackNumber > 0 ? QString::number(*w.trackNumber) : QString();
        if (!track.isEmpty() && w.trackTotal && *w.trackTotal > 0)
            track += QLatin1Char('/') + QString::number(*w.trackTotal);
        setList(map, "TRACKNUMBER", {track});
    }
    if (w.date)
        setList(map, "DATE", {formatDate(*w.date)});
    setList(map, "GENRE", w.genres);
}

void setApic(TL::ID3v2::Tag* tag, const TagWrite& w)
{
    if (w.coverAction == CoverAction::Keep)
        return;
    tag->removeFrames("APIC");
    if (w.coverAction == CoverAction::Set) {
        auto* frame = new TL::ID3v2::AttachedPictureFrame;
        frame->setMimeType(t(w.cover.mimeType));
        frame->setType(TL::ID3v2::AttachedPictureFrame::FrontCover);
        frame->setDescription(TL::String());
        frame->setPicture(toTL(w.cover.data));
        frame->setTextEncoding(TL::String::UTF8);
        tag->addFrame(frame);
    }
}

// Many files store the genre as an old numeric code ("7" for Hip-Hop). TagLib then sees no change when the same genre is
// "written" and keeps the code; the genre frame is therefore always replaced by one with the names.
void setGenreFrame(TL::ID3v2::Tag* tag, const TagWrite& w)
{
    tag->removeFrames("TCON");
    const QStringList clean = nonEmpty(w.genres);
    if (clean.isEmpty())
        return;
    auto* frame = new TL::ID3v2::TextIdentificationFrame("TCON", TL::String::UTF8);
    frame->setText(tl(clean));
    tag->addFrame(frame);
}

void forceUtf8(TL::ID3v2::Tag* tag)
{
    for (TL::ID3v2::Frame* frame : tag->frameList()) {
        if (auto* text = dynamic_cast<TL::ID3v2::TextIdentificationFrame*>(frame))
            text->setTextEncoding(TL::String::UTF8);
    }
}

TL::FLAC::Picture* makeFlacPicture(const CoverImage& c)
{
    auto* pic = new TL::FLAC::Picture;
    pic->setType(TL::FLAC::Picture::FrontCover);
    pic->setMimeType(t(c.mimeType));
    pic->setData(toTL(c.data));
    return pic;
}

Result<void> writeMp3(const QString& copy, const TagWrite& w)
{
    const TlPath p(copy);
    TL::MPEG::File f(p.name(), false);
    if (!f.isValid() || f.readOnly())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("cannot open the copy for writing"));
    TL::ID3v2::Tag* tag = f.ID3v2Tag(true);
    TL::PropertyMap map = f.properties();  // includes values that only exist in ID3v1
    applyProperties(map, w);
    tag->setProperties(map);
    setGenreFrame(tag, w);
    forceUtf8(tag);
    setApic(tag, w);
    // ID3v2.4 with UTF-8; ID3v1 (and APE) are stripped so no stale mojibake survives.
    if (!f.save(TL::MPEG::File::ID3v2, TL::File::StripOthers, TL::ID3v2::v4, TL::File::DoNotDuplicate))
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("TagLib could not save the MP3 tags"));
    return {};
}

Result<void> writeWav(const QString& copy, const TagWrite& w)
{
    const TlPath p(copy);
    TL::RIFF::WAV::File f(p.name(), false);
    if (!f.isValid() || f.readOnly())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("cannot open the copy for writing"));
    TL::ID3v2::Tag* tag = f.ID3v2Tag();
    TL::PropertyMap map = f.properties();
    applyProperties(map, w);
    tag->setProperties(map);
    setGenreFrame(tag, w);
    forceUtf8(tag);
    setApic(tag, w);
    if (!f.save(TL::RIFF::WAV::File::ID3v2, TL::File::StripOthers, TL::ID3v2::v4))
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("TagLib could not save the WAV tags"));
    return {};
}

Result<void> writeFlac(const QString& copy, const TagWrite& w)
{
    const TlPath p(copy);
    TL::FLAC::File f(p.name(), false);
    if (!f.isValid() || f.readOnly())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("cannot open the copy for writing"));
    TL::PropertyMap map = f.properties();
    applyProperties(map, w);
    f.setProperties(map);
    if (w.coverAction != CoverAction::Keep) {
        f.removePictures();
        if (w.coverAction == CoverAction::Set)
            f.addPicture(makeFlacPicture(w.cover));
    }
    if (!f.save())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("TagLib could not save the FLAC tags"));
    return {};
}

Result<void> writeOgg(const QString& copy, const TagWrite& w)
{
    const TlPath p(copy);
    TL::Ogg::Vorbis::File f(p.name(), false);
    if (!f.isValid() || f.readOnly())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("cannot open the copy for writing"));
    TL::PropertyMap map = f.properties();
    applyProperties(map, w);
    f.setProperties(map);
    if (w.coverAction != CoverAction::Keep) {
        f.tag()->removeAllPictures();
        if (w.coverAction == CoverAction::Set)
            f.tag()->addPicture(makeFlacPicture(w.cover));
    }
    if (!f.save())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("TagLib could not save the OGG tags"));
    return {};
}

Result<void> writeM4a(const QString& copy, const TagWrite& w)
{
    const TlPath p(copy);
    TL::MP4::File f(p.name(), false);
    if (!f.isValid() || f.readOnly())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("cannot open the copy for writing"));
    TL::PropertyMap map = f.properties();
    applyProperties(map, w);
    f.setProperties(map);
    if (w.coverAction != CoverAction::Keep) {
        f.tag()->removeItem("covr");
        if (w.coverAction == CoverAction::Set) {
            const auto format = w.cover.mimeType == QLatin1String("image/png") ? TL::MP4::CoverArt::PNG : TL::MP4::CoverArt::JPEG;
            f.tag()->setItem("covr", TL::MP4::Item(TL::MP4::CoverArtList{TL::MP4::CoverArt(format, toTL(w.cover.data))}));
        }
    }
    if (!f.save())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("TagLib could not save the M4A tags"));
    return {};
}

Result<void> writeWma(const QString& copy, const TagWrite& original)
{
    // ASF has a single "Author" string: several artists are written separated by ";" (read back split again).
    TagWrite w = original;
    if (w.artists.size() > 1)
        w.artists = {w.artists.join(QLatin1Char(';'))};
    const TlPath p(copy);
    TL::ASF::File f(p.name(), false);
    if (!f.isValid() || f.readOnly())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("cannot open the copy for writing"));
    TL::PropertyMap map = f.properties();
    applyProperties(map, w);
    f.setProperties(map);
    if (w.coverAction != CoverAction::Keep) {
        f.tag()->removeItem("WM/Picture");
        if (w.coverAction == CoverAction::Set) {
            TL::ASF::Picture pic;
            pic.setType(TL::ASF::Picture::FrontCover);
            pic.setMimeType(t(w.cover.mimeType));
            pic.setPicture(toTL(w.cover.data));
            f.tag()->addAttribute("WM/Picture", TL::ASF::Attribute(pic));
        }
    }
    if (!f.save())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("TagLib could not save the WMA tags"));
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------

TagService::TagService(IAtomicReplace& replace, IFileReleaser* releaser) : m_replace(replace), m_releaser(releaser)
{
    installTagLibLogging();
}

bool TagService::isSupportedExtension(const QString& suffix)
{
    return formatOf(QStringLiteral("x.") + suffix) != Fmt::Unknown;
}

QStringList TagService::supportedExtensions()
{
    return {QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
            QStringLiteral("m4a"), QStringLiteral("m4b"), QStringLiteral("mp4"), QStringLiteral("wav"),
            QStringLiteral("wma")};
}

Result<TrackTags> TagService::read(const QString& absolutePath)
{
    return readByFormat(absolutePath, formatOf(absolutePath));
}

Result<std::optional<CoverImage>> TagService::readCover(const QString& absolutePath)
{
    const Fmt fmt = formatOf(absolutePath);
    if (fmt == Fmt::Unknown)
        return Error::make(ErrorCode::UnsupportedFile, absolutePath);
    if (!QFileInfo(absolutePath).isFile())
        return Error::make(ErrorCode::NotFound, absolutePath);
    const TlPath p(absolutePath);
    TL::FileStream stream(p.name(), true);
    if (!stream.isOpen())
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot open ") + absolutePath);

    switch (fmt) {
    case Fmt::Mp3: {
        TL::MPEG::File f(&stream, false);
        if (f.isValid())
            return coverOf(f);
        break;
    }
    case Fmt::Flac: {
        TL::FLAC::File f(&stream, false);
        if (f.isValid())
            return coverOf(f);
        break;
    }
    case Fmt::Ogg: {
        TL::Ogg::Vorbis::File f(&stream, false);
        if (f.isValid())
            return coverOf(f);
        break;
    }
    case Fmt::M4a: {
        TL::MP4::File f(&stream, false);
        if (f.isValid())
            return coverOf(f);
        break;
    }
    case Fmt::Wav: {
        TL::RIFF::WAV::File f(&stream, false);
        if (f.isValid())
            return coverOf(f);
        break;
    }
    case Fmt::Wma: {
        TL::ASF::File f(&stream, false);
        if (f.isValid())
            return coverOf(f);
        break;
    }
    case Fmt::Unknown:
        break;
    }
    return Error::make(ErrorCode::UnsupportedFile, QStringLiteral("not a valid audio file: ") + absolutePath);
}

Result<void> TagService::writeToCopy(const QString& copyPath, const QString& originalPath, const TagWrite& request)
{
    switch (formatOf(originalPath)) {
    case Fmt::Mp3: SEMPLICE_TRY(writeMp3(copyPath, request)); break;
    case Fmt::Flac: SEMPLICE_TRY(writeFlac(copyPath, request)); break;
    case Fmt::Ogg: SEMPLICE_TRY(writeOgg(copyPath, request)); break;
    case Fmt::M4a: SEMPLICE_TRY(writeM4a(copyPath, request)); break;
    case Fmt::Wav: SEMPLICE_TRY(writeWav(copyPath, request)); break;
    case Fmt::Wma: SEMPLICE_TRY(writeWma(copyPath, request)); break;
    case Fmt::Unknown: return Error::make(ErrorCode::UnsupportedFile, originalPath);
    }

    // Read the copy back before it replaces anything: a silently dropped field must never reach the library.
    auto back = readByFormat(copyPath, formatOf(originalPath));  // the copy's name has no audio extension, the format is explicit
    if (!back.ok())
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("the written file cannot be read back: ") + back.error().devMessage);
    const TrackTags& r = back.value();
    auto mismatch = [](const char* field) {
        return Error::make(ErrorCode::TagWriteFailed, QStringLiteral("verification failed for ") + QLatin1String(field));
    };
    if (r.title != request.title)
        return mismatch("title");
    if (r.artists != nonEmpty(request.artists) && formatOf(originalPath) != Fmt::Wma)
        return mismatch("artists");
    if (formatOf(originalPath) == Fmt::Wma && r.artists.join(QLatin1Char(';')) != nonEmpty(request.artists).join(QLatin1Char(';')))
        return mismatch("artists");
    {  // genres: the same names, whatever the order or the capitals
        auto folded = [](const QStringList& list) {
            QStringList out;
            for (const QString& g : list)
                if (!out.contains(g, Qt::CaseInsensitive))
                    out << g.toCaseFolded();
            out.sort();
            return out;
        };
        if (folded(r.genres) != folded(nonEmpty(request.genres)))
            return mismatch("genres");
    }
    if (request.album && r.album != *request.album)
        return mismatch("album");
    if (request.date && request.date->year > 0 && r.date.year != request.date->year)
        return mismatch("date");
    if (request.coverAction == CoverAction::Set && !r.hasCover)
        return mismatch("cover");
    if (request.coverAction == CoverAction::Remove && r.hasCover)
        return mismatch("cover");
    return {};
}

Result<void> TagService::write(const QString& absolutePath, const TagWrite& request)
{
    const Fmt fmt = formatOf(absolutePath);
    if (fmt == Fmt::Unknown)
        return Error::make(ErrorCode::UnsupportedFile, absolutePath);
    if (!QFileInfo(absolutePath).isFile())
        return Error::make(ErrorCode::NotFound, absolutePath);
    if (request.title.trimmed().isEmpty())
        return Error::make(ErrorCode::NameEmpty, QStringLiteral("track title is empty"));
    if (request.coverAction == CoverAction::Set && !CoverImage::fromBytes(request.cover.data))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("cover is not a JPEG or PNG"));

    // the track that plays is written later (its tags stay marked "out of sync" and the player asks for them when it moves on)
    if (m_releaser && m_releaser->inUse(absolutePath))
        return Error::make(ErrorCode::FileLocked, QStringLiteral("the file is playing: its tags are written later"));

    const QString temp = absolutePath + QStringLiteral(".semplice-tagtmp-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    if (!QFile::copy(absolutePath, temp))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot create a temporary copy of ") + absolutePath);

    auto written = writeToCopy(temp, absolutePath, request);
    if (!written.ok()) {
        (void)ownfiles::removeFile(temp);
        return written.error();
    }
    // The copy was made and tagged while the file kept playing; only the swap itself needs the player to let go of it
    // (a few milliseconds instead of the whole write).
    if (m_releaser) {
        auto released = m_releaser->releaseFile(absolutePath);
        if (!released.ok()) {
            (void)ownfiles::removeFile(temp);
            return released.error();
        }
    }
    auto replaced = m_replace.replaceFile(temp, absolutePath);
    if (m_releaser)
        m_releaser->fileReady(absolutePath);
    if (!replaced.ok()) {
        (void)ownfiles::removeFile(temp);  // the original was not touched
        return replaced.error();
    }
    return {};
}

Result<void> TagService::writeNewFile(const QString& absolutePath, const TagWrite& request)
{
    if (formatOf(absolutePath) == Fmt::Unknown)
        return Error::make(ErrorCode::UnsupportedFile, absolutePath);
    if (!QFileInfo(absolutePath).isFile())
        return Error::make(ErrorCode::NotFound, absolutePath);
    if (request.title.trimmed().isEmpty())
        return Error::make(ErrorCode::NameEmpty, QStringLiteral("track title is empty"));
    if (request.coverAction == CoverAction::Set && !CoverImage::fromBytes(request.cover.data))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("cover is not a JPEG or PNG"));
    return writeToCopy(absolutePath, absolutePath, request);  // "copy" and original are the same new file
}

int TagService::removeStaleTempFiles(const QString& libraryRoot)
{
    int cleaned = 0;
    for (const QString& top : {QStringLiteral("Artists"), QStringLiteral("Playlists")}) {
        const QString base = QDir(libraryRoot).filePath(top);
        if (!QFileInfo::exists(base))
            continue;
        QDirIterator it(base, {QStringLiteral("*.semplice-tagtmp-*"), QStringLiteral("*.semplice-old-*")}, QDir::Files | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const int marker = path.lastIndexOf(QStringLiteral(".semplice-old-"));
            if (marker > 0) {
                const QString original = path.left(marker);
                // Crash in the middle of a replace: the original was set aside but the new file never arrived.
                if (!QFileInfo::exists(original) && QFile::rename(path, original)) {
                    ++cleaned;
                    continue;
                }
            }
            if (ownfiles::removeFile(path).ok())
                ++cleaned;
        }
    }
    return cleaned;
}

}  // namespace semplice
