#pragma once

#include "core/domain/ReleaseDate.h"

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace semplice {

// A cover picture, JPEG or PNG only (SPEC 1).
struct CoverImage {
    QByteArray data;
    QString mimeType;  // "image/jpeg" | "image/png"

    // Accepts the bytes only if they really start like a JPEG or a PNG.
    static std::optional<CoverImage> fromBytes(const QByteArray& bytes);
    QString extension() const;  // "jpg" | "png"
    bool operator==(const CoverImage&) const = default;
};

// Field names used in TrackTags::legacy8bit.
namespace tagfield {
inline const QString Title = QStringLiteral("title");
inline const QString Artists = QStringLiteral("artists");
inline const QString Album = QStringLiteral("album");
inline const QString AlbumArtists = QStringLiteral("albumArtists");
inline const QString Genres = QStringLiteral("genres");
}  // namespace tagfield

// What a file says about itself, exactly as read.
struct TrackTags {
    QString title;
    QStringList artists;       // multi-value
    QString album;
    QStringList albumArtists;  // multi-value
    int trackNumber = 0;       // 0 = unknown
    int trackTotal = 0;
    ReleaseDate date;          // year 0 = unknown; month/day 0 = not given
    QStringList genres;        // raw strings; GenreMap turns them into vocabulary keys

    qint64 durationMs = 0;
    QString codec;             // mp3, flac, vorbis, aac, alac, pcm, wma
    int bitrateKbps = 0;
    bool hasCover = false;

    // Fields whose text was stored as legacy 8-bit (ID3v1 or an ISO-8859-1 declared ID3v2 frame):
    // the only ones the EncodingFixer may touch (SPEC 6.3). Everything else is trusted.
    QSet<QString> legacy8bit;
};

enum class CoverAction { Keep, Remove, Set };

// What we want a file to say (SPEC 6.2). std::nullopt = leave that part of the file untouched.
struct TagWrite {
    QString title;                                  // always written
    QStringList artists;                            // always written (multi-value)
    std::optional<QString> album;                   // playlist tracks: nullopt = keep their album fields
    std::optional<QStringList> albumArtists;
    std::optional<int> trackNumber;
    std::optional<int> trackTotal;
    std::optional<ReleaseDate> date;
    QStringList genres;                             // canonical English names; empty list removes the field
    CoverAction coverAction = CoverAction::Keep;
    CoverImage cover;                               // used when coverAction == Set
};

}  // namespace semplice
