#pragma once

#include "core/domain/Types.h"
#include "core/storage/IFileStorage.h"

#include <QSet>

#include <optional>

namespace semplice {

// ---- inputs ---------------------------------------------------------------------------------

struct CreditDraft {
    ArtistId artist;       // either an existing artist ...
    QString name;          // ... or a name (found, or created hidden, inside the same transaction)
    CreditRole role = CreditRole::Main;

    CreditDraft() = default;
    CreditDraft(ArtistId a, CreditRole r = CreditRole::Main) : artist(a), role(r) {}
    CreditDraft(const QString& n, CreditRole r = CreditRole::Main) : name(n), role(r) {}
};

// A track about to enter the library. If `contentHash` is already known the existing track is reused
// (duplicate detection by hash, SPEC 6.1) and the other fields are ignored.
struct TrackDraft {
    QString title;
    qint64 durationMs = 0;
    QString extension = QStringLiteral("mp3");
    QString codec;
    QByteArray contentHash;  // SHA-256 of the source bytes, 32 bytes
    QByteArray sourceHash;   // SHA-256 of the file at `sourcePath` when it differs from contentHash (a file unpacked from a share package: the
                             // identity is that of the original file, the bytes are those of the shared copy); empty = same as contentHash
    qint64 fileSize = 0;
    qint64 fileMtime = 0;
    bool hasEmbeddedCover = false;
    QList<CreditDraft> credits;
    QStringList tags;

    // Where the audio file comes from. Empty = the file is placed by someone else (M1 tests).
    QString sourcePath;            // absolute
    bool moveIntoLibrary = false;  // Rescan: the file already lies inside the library folder and is moved
};

struct TrackInput {
    std::optional<TrackId> existing;  // use an existing track instead of a draft
    TrackDraft draft;
};

struct ArtistDraft {
    QString name;
    bool hidden = false;
    QString avatarPath;
    QStringList tags;
};

struct AlbumDraft {
    ArtistId primaryArtist;
    QString primaryArtistName;     // used when primaryArtist is not set
    QList<ArtistId> coAuthors;
    QStringList coAuthorNames;     // additional co-authors given by name
    QString title;
    AlbumType type = AlbumType::Album;
    ReleaseDate date;
    QStringList tags;
    QString coverPath;             // absolute path of an image file to copy in as album_cover.<ext>
    QList<TrackInput> tracks;
};

struct PlaylistDraft {
    QString name;
    QString coverPath;             // absolute path of an image file to copy in as playlist_cover.<ext>
    QList<TrackInput> tracks;
};

struct ArtistPatch {
    std::optional<QString> name;        // rename = folder rename + every file path + file tags
    std::optional<QString> avatarPath;  // absolute image file to copy in; empty string removes the avatar
    std::optional<QStringList> tags;
    std::optional<bool> userHidden;     // Hide / Show in the library
};

struct AlbumPatch {
    std::optional<QString> title;
    std::optional<AlbumType> type;
    std::optional<ReleaseDate> date;
    std::optional<QStringList> tags;
    std::optional<QString> coverPath;       // absolute image file to copy in; empty string removes the cover
    std::optional<ArtistId> primaryArtist;  // moves the folder under that artist
    std::optional<QList<ArtistId>> coAuthors;
    std::optional<QList<TrackId>> trackOrder;  // must list exactly the album's tracks
};

struct PlaylistPatch {
    std::optional<QString> name;
    std::optional<QString> coverPath;       // absolute image file; empty string removes the cover
    std::optional<QList<TrackId>> trackOrder;
};

struct TrackPatch {
    std::optional<QString> title;
    std::optional<QList<CreditDraft>> credits;  // replaces the credits (names allowed: new artists are created hidden)
    std::optional<QStringList> tags;
};

// ---- outputs --------------------------------------------------------------------------------

// What changed; consumed by the search index and later by view-models.
struct ChangeSet {
    QSet<ArtistId> artists;
    QSet<AlbumId> albums;
    QSet<PlaylistId> playlists;
    QSet<TrackId> tracks;

    bool isEmpty() const { return artists.isEmpty() && albums.isEmpty() && playlists.isEmpty() && tracks.isEmpty(); }
    bool operator==(const ChangeSet&) const = default;
};

// The complete outcome of one operation. `planDeleteX()` returns the same structure for an operation
// that was executed and rolled back, so a confirmation dialog can describe exactly what will happen.
struct OperationReport {
    QList<TrackId> tracksDeleted;       // file goes to the Recycle Bin
    QList<TrackId> tracksKept;          // left a container but still exist elsewhere
    QList<TrackId> tracksMoved;         // physical file moved to another home location
    QList<AlbumId> albumsDeleted;
    QList<PlaylistId> playlistsDeleted;
    QList<AlbumId> albumsRehomed;       // primary credit passed to a co-author (SPEC 4.3)
    QList<ArtistId> artistsDeleted;
    QList<ArtistId> artistsHidden;      // became hidden (kept for credits)
    QList<ArtistId> artistsMadeVisible;
    QList<TrackId> tracksCreated;
    QList<TrackId> tracksReused;        // a draft whose content hash was already in the library
    QList<TrackId> tagWriteFailed;      // file tags could not be written (tags_dirty stays set)
    QStringList warnings;               // human-readable, not translated (logged; the UI builds banners from the lists)
    QList<FileOp> fileOps;
    ChangeSet changes;

    bool operator==(const OperationReport&) const = default;
};

using DeletePlan = OperationReport;

}  // namespace semplice
