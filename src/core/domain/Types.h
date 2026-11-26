#pragma once

#include "core/domain/Ids.h"
#include "core/domain/ReleaseDate.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace semplice {

enum class AlbumType { Single, Album, Ep, Live, Compilation, Other };
enum class CreditRole { Main, Feat };
enum class ContainerKind { Album, Playlist };

QString toDbString(AlbumType type);
std::optional<AlbumType> albumTypeFromString(const QString& text);
QString toDbString(CreditRole role);
std::optional<CreditRole> creditRoleFromString(const QString& text);
QString toDbString(ContainerKind kind);

// Reserved folder name of the system album (SPEC 4.1, 5.2). The title itself is never stored.
inline const QString kUnsortedDirName = QStringLiteral("Unsorted tracks");

struct Artist {
    ArtistId id;
    QString name;
    QString dirName;  // empty while hidden (no folder)
    bool hidden = false;
    bool userHidden = false;  // hidden by hand: shown only with the Hidden filter, grey, no Home carousel
    QString avatarPath;
    bool tagsSeeded = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct Album {
    AlbumId id;
    ArtistId primaryArtist;
    QString title;  // empty for the Unsorted album (translated at display time)
    QString dirName;
    AlbumType type = AlbumType::Album;
    bool isUnsorted = false;
    ReleaseDate date;
    QString coverPath;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct Playlist {
    PlaylistId id;
    QString name;
    QString dirName;
    QString coverPath;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

// The place where a track's single physical file lives (SPEC 4.2).
struct HomeRef {
    ContainerKind kind = ContainerKind::Album;
    qint64 id = 0;
    bool operator==(const HomeRef&) const = default;
};

struct Track {
    TrackId id;
    QString title;
    qint64 durationMs = 0;
    QString filePath;  // library-relative, forward slashes
    qint64 fileSize = 0;
    qint64 fileMtime = 0;
    QString codec;
    QByteArray contentHash;  // SHA-256 of the source bytes
    bool hasEmbeddedCover = false;
    bool tagsDirty = false;  // the file tags may differ from the database (SPEC 6.2)
    int playCount = 0;
    std::optional<qint64> lastPlayedAt;
    HomeRef home;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct TrackCredit {
    ArtistId artist;
    int position = 0;  // 0 = first credited artist
    CreditRole role = CreditRole::Main;
    bool operator==(const TrackCredit&) const = default;
};

struct CoAuthor {
    ArtistId artist;
    int position = 1;  // >= 1
};

struct Membership {
    ContainerKind kind = ContainerKind::Album;
    qint64 containerId = 0;
    int position = 0;
    qint64 addedAt = 0;
};

}  // namespace semplice
