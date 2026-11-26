#pragma once

#include "core/db/Database.h"
#include "core/domain/Types.h"

#include <QList>
#include <QSet>
#include <QString>

namespace semplice {

enum class LibraryTab { Playlists = 0, Artists = 1, Albums = 2 };
enum class SortKey { Alphabet, Recent, Year };
// Ascending = the natural order of the key: A -> Z, oldest first, lowest year first. The UI doc's
// "down arrow" on Alphabet is Ascending (A -> Z), the "up arrow" Descending; the default for Recent
// is Descending (newest first).
enum class SortDirection { Ascending, Descending };

struct LibrarySort {
    SortKey key = SortKey::Recent;
    SortDirection direction = SortDirection::Descending;
    bool operator==(const LibrarySort&) const = default;
};

struct ArtistRow {
    ArtistId id;
    QString name;
    QString coverPath;  // avatar, library-relative; empty = letter placeholder
    bool userHidden = false;
};

struct AlbumRow {
    AlbumId id;
    QString title;
    ArtistId primaryArtist;
    QString artists;    // "Primary, Co-author, ..."
    AlbumType type = AlbumType::Album;
    int year = 0;
    QString coverPath;
};

struct PlaylistRow {
    PlaylistId id;
    QString name;
    QString coverPath;
};

// The three library lists (SPEC 8.5). One statement each, so even 200k rows come back in one go.
class LibraryQueries {
public:
    explicit LibraryQueries(Database& db) : m_db(db) {}

    // Hidden artists never appear.
    // `onlyHidden`: the artists hidden by hand instead of the others
    Result<QList<ArtistRow>> artists(const LibrarySort& sort, bool onlyHidden = false);
    // `types` empty = every type; the Unsorted album is never listed.
    Result<QList<AlbumRow>> albums(const LibrarySort& sort, const QSet<AlbumType>& types = {});
    Result<QList<PlaylistRow>> playlists(const LibrarySort& sort);

    static bool isAllowed(LibraryTab tab, SortKey key);  // Artists/Playlists: Alphabet, Recent; Albums: + Year

private:
    Database& m_db;
};

}  // namespace semplice
