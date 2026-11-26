#include "TestAudioFiles.h"

#include <QDir>
#include <QFile>

namespace semplice::testing {

namespace {

void put32le(QByteArray& b, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put16le(QByteArray& b, quint16 v)
{
    b.append(static_cast<char>(v & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
}
void put64le(QByteArray& b, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put32be(QByteArray& b, quint32 v)
{
    for (int i = 3; i >= 0; --i)
        b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
}

// ---- MP3: four silent MPEG-1 Layer III frames, 128 kbit/s, 44.1 kHz stereo (417 bytes each) --------
QByteArray makeMp3()
{
    QByteArray out;
    for (int i = 0; i < 4; ++i) {
        out.append(QByteArray::fromHex("FFFB9000"));
        out.append(QByteArray(417 - 4, '\0'));
    }
    return out;
}

// ---- WAV: 0.1 s of 8 kHz mono 16-bit silence --------------------------------------------------------
QByteArray makeWav()
{
    const int samples = 800;
    QByteArray data(samples * 2, '\0');
    QByteArray out("RIFF");
    put32le(out, static_cast<quint32>(36 + data.size()));
    out.append("WAVEfmt ");
    put32le(out, 16);
    put16le(out, 1);      // PCM
    put16le(out, 1);      // mono
    put32le(out, 8000);   // sample rate
    put32le(out, 16000);  // byte rate
    put16le(out, 2);      // block align
    put16le(out, 16);     // bits
    out.append("data");
    put32le(out, static_cast<quint32>(data.size()));
    out.append(data);
    return out;
}

// ---- FLAC: signature + STREAMINFO only (44.1 kHz, stereo, 16 bit, one second) ------------------------
QByteArray makeFlac()
{
    QByteArray out("fLaC");
    out.append(static_cast<char>(0x80));  // last metadata block, type 0 = STREAMINFO
    out.append(QByteArray::fromHex("000022"));  // length 34
    out.append(QByteArray::fromHex("10001000"));  // min/max block size 4096
    out.append(QByteArray(6, '\0'));              // min/max frame size unknown
    const quint64 packed = (static_cast<quint64>(44100) << 44) | (static_cast<quint64>(1) << 41) |
                           (static_cast<quint64>(15) << 36) | static_cast<quint64>(44100);
    for (int i = 7; i >= 0; --i)
        out.append(static_cast<char>((packed >> (8 * i)) & 0xFF));
    out.append(QByteArray(16, '\0'));  // MD5
    return out;
}

// ---- OGG Vorbis: three header packets + one tiny audio page -------------------------------------------
quint32 oggCrc(const QByteArray& page)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 r = i << 24;
            for (int k = 0; k < 8; ++k)
                r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
            table[i] = r;
        }
        ready = true;
    }
    quint32 crc = 0;
    for (const char c : page)
        crc = (crc << 8) ^ table[((crc >> 24) & 0xFF) ^ static_cast<quint8>(c)];
    return crc;
}

QByteArray oggPage(quint8 headerType, quint64 granule, quint32 sequence, const QList<QByteArray>& packets)
{
    QByteArray segments;
    QByteArray body;
    for (const QByteArray& p : packets) {
        int remaining = static_cast<int>(p.size());
        while (remaining >= 255) {
            segments.append(static_cast<char>(255));
            remaining -= 255;
        }
        segments.append(static_cast<char>(remaining));  // < 255 ends the packet
        body.append(p);
    }
    QByteArray page("OggS");
    page.append('\0');                              // version
    page.append(static_cast<char>(headerType));
    put64le(page, granule);
    put32le(page, 0x53454D50);                      // serial number
    put32le(page, sequence);
    put32le(page, 0);                               // CRC placeholder
    page.append(static_cast<char>(segments.size()));
    page.append(segments);
    page.append(body);
    const quint32 crc = oggCrc(page);
    for (int i = 0; i < 4; ++i)
        page[22 + i] = static_cast<char>((crc >> (8 * i)) & 0xFF);
    return page;
}

QByteArray makeOgg()
{
    QByteArray id("\x01vorbis", 7);
    put32le(id, 0);        // vorbis version
    id.append(static_cast<char>(2));      // channels
    put32le(id, 44100);    // sample rate
    put32le(id, 0);        // bitrate max
    put32le(id, 128000);   // nominal
    put32le(id, 0);        // min
    id.append(static_cast<char>(0xB8));   // blocksizes 256 / 2048
    id.append(static_cast<char>(1));      // framing

    QByteArray comment("\x03vorbis", 7);
    const QByteArray vendor("semplice-test");
    put32le(comment, static_cast<quint32>(vendor.size()));
    comment.append(vendor);
    put32le(comment, 0);                  // no comments yet
    comment.append(static_cast<char>(1));  // framing bit

    QByteArray setup("\x05vorbis", 7);
    setup.append(QByteArray(12, '\x01'));

    QByteArray out;
    out.append(oggPage(0x02, 0, 0, {id}));
    out.append(oggPage(0x00, 0, 1, {comment, setup}));
    out.append(oggPage(0x04, 44100, 2, {QByteArray(1, '\x00')}));  // end of stream, one second
    return out;
}

// ---- M4A: ftyp + moov(mvhd) + mdat ----------------------------------------------------------------------
QByteArray box(const QByteArray& type, const QByteArray& payload)
{
    QByteArray out;
    put32be(out, static_cast<quint32>(8 + payload.size()));
    out.append(type);
    out.append(payload);
    return out;
}

QByteArray makeM4a()
{
    QByteArray ftyp("M4A ");
    put32be(ftyp, 0);
    ftyp.append("M4A mp42isom");

    QByteArray mvhd;
    put32be(mvhd, 0);        // version + flags
    put32be(mvhd, 0);        // creation
    put32be(mvhd, 0);        // modification
    put32be(mvhd, 1000);     // timescale
    put32be(mvhd, 1000);     // duration: one second
    put32be(mvhd, 0x00010000);  // rate 1.0
    mvhd.append(QByteArray::fromHex("0100"));  // volume 1.0
    mvhd.append(QByteArray(10, '\0'));          // reserved
    const char matrix[36] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0, 0, 0};
    mvhd.append(matrix, 36);
    mvhd.append(QByteArray(24, '\0'));          // pre-defined
    put32be(mvhd, 2);                           // next track id

    QByteArray out;
    out.append(box("ftyp", ftyp));
    out.append(box("moov", box("mvhd", mvhd)));
    out.append(box("mdat", QByteArray(16, '\0')));
    return out;
}

// ---- WMA: ASF header object with a file-properties object, then a data object ------------------------------
QByteArray makeWma()
{
    const QByteArray headerGuid = QByteArray::fromHex("3026B2758E66CF11A6D900AA0062CE6C");
    const QByteArray dataGuid = QByteArray::fromHex("3626B2758E66CF11A6D900AA0062CE6C");
    const QByteArray fileGuid = QByteArray::fromHex("A1DCAB8C47A9CF118EE400C00C205365");

    QByteArray props = fileGuid;
    put64le(props, 104);                      // object size
    props.append(QByteArray(16, '\x07'));     // file id
    put64le(props, 0);                        // file size (unknown)
    put64le(props, 0);                        // creation date
    put64le(props, 1);                        // data packet count
    put64le(props, 10000000);                 // play duration: 1 s in 100 ns units
    put64le(props, 10000000);                 // send duration
    put64le(props, 0);                        // preroll
    put32le(props, 2);                        // flags (seekable)
    put32le(props, 64);                       // min packet size
    put32le(props, 64);                       // max packet size
    put32le(props, 128000);                   // max bitrate

    // Stream properties object (TagLib refuses an ASF file without it): one WMA v2 audio stream.
    QByteArray stream = QByteArray::fromHex("9107DCB7B7A9CF118EE600C00C205365");
    put64le(stream, 24 + 72);
    stream.append(QByteArray::fromHex("409E69F84D5BCF11A8FD00805F5C442B"));  // audio media
    stream.append(QByteArray::fromHex("50CDC3BF8F61CF118BB200AA00B4E220"));  // no error correction
    put64le(stream, 0);       // time offset
    put32le(stream, 18);      // type-specific data length
    put32le(stream, 0);       // error correction data length
    put16le(stream, 1);       // flags: stream number 1
    put32le(stream, 0);       // reserved
    put16le(stream, 0x0161);  // WMA v2
    put16le(stream, 2);       // channels
    put32le(stream, 44100);   // sample rate
    put32le(stream, 16000);   // average bytes per second
    put16le(stream, 1);       // block align
    put16le(stream, 16);      // bits per sample
    put16le(stream, 0);       // extra data size

    QByteArray out = headerGuid;
    put64le(out, 30 + props.size() + stream.size());
    put32le(out, 2);                          // two sub-objects
    out.append('\x01');
    out.append('\x02');
    out.append(props);
    out.append(stream);

    QByteArray data = dataGuid;
    put64le(data, 50);
    data.append(QByteArray(16, '\x07'));
    put64le(data, 1);
    data.append(QByteArray(2, '\x01'));
    out.append(data);
    return out;
}

void putSyncsafe(QByteArray& b, quint32 v)
{
    for (int i = 3; i >= 0; --i)
        b.append(static_cast<char>((v >> (7 * i)) & 0x7F));
}

}  // namespace

QList<AudioFormat> allAudioFormats()
{
    return {AudioFormat::Mp3, AudioFormat::Flac, AudioFormat::Ogg, AudioFormat::M4a, AudioFormat::Wav, AudioFormat::Wma};
}

QString extensionFor(AudioFormat f)
{
    switch (f) {
    case AudioFormat::Mp3: return QStringLiteral("mp3");
    case AudioFormat::Flac: return QStringLiteral("flac");
    case AudioFormat::Ogg: return QStringLiteral("ogg");
    case AudioFormat::M4a: return QStringLiteral("m4a");
    case AudioFormat::Wav: return QStringLiteral("wav");
    case AudioFormat::Wma: return QStringLiteral("wma");
    }
    return {};
}

QString nameOf(AudioFormat f)
{
    return extensionFor(f).toUpper();
}

QByteArray tinyAudio(AudioFormat f, int seed)
{
    QByteArray b;
    switch (f) {
    case AudioFormat::Mp3: b = makeMp3(); break;
    case AudioFormat::Flac: b = makeFlac(); break;
    case AudioFormat::Ogg: b = makeOgg(); break;
    case AudioFormat::M4a: b = makeM4a(); break;
    case AudioFormat::Wav: b = makeWav(); break;
    case AudioFormat::Wma: b = makeWma(); break;
    }
    // A different seed gives different bytes (so a different content hash) without changing the structure.
    if (seed != 0 && b.size() > 200) {
        if (f == AudioFormat::Mp3) {
            b[100] = static_cast<char>(seed & 0xFF);
            b[101] = static_cast<char>((seed >> 8) & 0xFF);
        } else if (f == AudioFormat::Flac || f == AudioFormat::Wav) {
            b[b.size() - 1] = static_cast<char>(seed & 0xFF);
            b[b.size() - 2] = static_cast<char>((seed >> 8) & 0xFF);
        }
    }
    return b;
}

QString writeTinyAudio(const QString& folder, const QString& baseName, AudioFormat f, int seed)
{
    QDir().mkpath(folder);
    const QString path = QDir(folder).filePath(baseName + QLatin1Char('.') + extensionFor(f));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(tinyAudio(f, seed));
    return path;
}

QByteArray buildId3v2(int version, const QList<RawId3Frame>& frames)
{
    QByteArray body;
    for (const RawId3Frame& f : frames) {
        QByteArray payload(1, f.encoding);
        payload.append(f.text);
        body.append(f.id);
        if (version == 4)
            putSyncsafe(body, static_cast<quint32>(payload.size()));
        else
            put32be(body, static_cast<quint32>(payload.size()));
        body.append(QByteArray(2, '\0'));  // flags
        body.append(payload);
    }
    QByteArray tag("ID3");
    tag.append(static_cast<char>(version));
    tag.append('\0');       // revision
    tag.append('\0');       // flags
    putSyncsafe(tag, static_cast<quint32>(body.size()));
    tag.append(body);
    return tag;
}

QByteArray buildId3v1(const QByteArray& title, const QByteArray& artist, const QByteArray& album, const QByteArray& year,
                      quint8 genreId)
{
    auto field = [](const QByteArray& v, int size) {
        QByteArray f = v.left(size);
        f.append(QByteArray(size - f.size(), '\0'));
        return f;
    };
    QByteArray tag("TAG");
    tag.append(field(title, 30));
    tag.append(field(artist, 30));
    tag.append(field(album, 30));
    tag.append(field(year, 4));
    tag.append(QByteArray(30, '\0'));  // comment
    tag.append(static_cast<char>(genreId));
    return tag;
}

}  // namespace semplice::testing
