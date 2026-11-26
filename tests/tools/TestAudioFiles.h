#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace semplice::testing {

// Tiny but structurally valid audio files, built byte by byte (no encoder needed). They contain no
// tags; tests add tags with TagService or with the raw ID3 builders below. Not playable audio.
enum class AudioFormat { Mp3, Flac, Ogg, M4a, Wav, Wma };

QList<AudioFormat> allAudioFormats();
QString extensionFor(AudioFormat format);
QString nameOf(AudioFormat format);
// seed != 0 changes a few payload bytes (MP3, FLAC, WAV) so that files get distinct content hashes.
QByteArray tinyAudio(AudioFormat format, int seed = 0);
// Writes the file below `folder` and returns its absolute path.
QString writeTinyAudio(const QString& folder, const QString& baseName, AudioFormat format, int seed = 0);

// ---- raw ID3 building, for fixtures with a specific text encoding ---------------------------------
struct RawId3Frame {
    QByteArray id;      // "TIT2", "TPE1", ...
    char encoding = 0;  // 0 = ISO-8859-1, 1 = UTF-16 (with BOM), 3 = UTF-8 (v2.4 only)
    QByteArray text;    // the bytes exactly as they appear in the file
};
// version: 3 or 4. Returns the complete tag (header + frames), ready to be put before the audio.
QByteArray buildId3v2(int version, const QList<RawId3Frame>& frames);
// A 128 byte ID3v1 tag; text fields are truncated/padded to their field sizes.
QByteArray buildId3v1(const QByteArray& title, const QByteArray& artist, const QByteArray& album,
                      const QByteArray& year, quint8 genreId);

}  // namespace semplice::testing
