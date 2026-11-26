#pragma once

#include "core/db/Database.h"
#include "core/domain/Types.h"

namespace semplice {

// Persistence for albums, their co-authors and their track list.
class AlbumRepo {
public:
    explicit AlbumRepo(Database& db) : m_db(db) {}

    // Fills id; title_key derived from the title ('' for the Unsorted album).
    Result<AlbumId> insert(const Album& album);
    Result<std::optional<Album>> get(AlbumId id);
    Result<std::optional<Album>> findByTitle(ArtistId primary, const QString& title);
    Result<std::optional<Album>> unsortedOf(ArtistId primary);
    Result<QList<Album>> byPrimary(ArtistId primary);       // includes the Unsorted album
    Result<QList<Album>> byCoAuthor(ArtistId coAuthor);
    Result<QList<Album>> all();

    // Moves the album under another primary artist and/or renames it.
    Result<void> setOwnership(AlbumId id, ArtistId primary, const QString& title, const QString& dirName, qint64 now);
    Result<void> setCover(AlbumId id, const QString& coverPath, qint64 now);
    Result<void> setMeta(AlbumId id, AlbumType type, const ReleaseDate& date, qint64 now);
    // Rewrites the positions to 1..n in the given order (must contain exactly the album's tracks).
    Result<void> reorder(AlbumId id, const QList<TrackId>& order);
    Result<void> remove(AlbumId id);  // cascades album_track, album_artist, album_tag
    Result<bool> dirNameTaken(ArtistId primary, const QString& dirName);

    // Co-authors (positions 1..n).
    Result<QList<CoAuthor>> coAuthors(AlbumId id);
    Result<void> setCoAuthors(AlbumId id, const QList<CoAuthor>& coAuthors);  // replaces, renumbers 1..n
    Result<int> coAuthorCount(ArtistId artist);

    // Track list. Positions are kept 1..n by renumber().
    Result<QList<TrackId>> tracks(AlbumId id);  // in position order
    Result<int> trackCount(AlbumId id);
    Result<void> addTrack(AlbumId id, TrackId track, int position, qint64 addedAt);  // shifts others down
    Result<void> removeTrack(AlbumId id, TrackId track);
    Result<void> renumber(AlbumId id);
    Result<int> positionOf(AlbumId id, TrackId track);  // 0 if absent

private:
    Database& m_db;
};

}  // namespace semplice
