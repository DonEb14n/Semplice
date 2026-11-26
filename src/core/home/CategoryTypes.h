#pragma once

#include "core/domain/Types.h"

#include <QList>
#include <QString>

#include <optional>

namespace semplice {

enum class CategoryType { ArtistDiscography, Year, Decade, Genre, YourPlaylists, YourAlbums };

// One carousel on the Home page. `arg` identifies the instance (artist id, year, first year of the decade, genre key).
struct CategorySpec {
    CategoryType type = CategoryType::YourAlbums;
    QString arg;
    QString label;  // artist name for the discography; empty otherwise (the view-model builds the title)

    // "artist:12", "year:2019", "decade:1990", "genre:rock", "playlists", "albums"
    QString key() const;
    static std::optional<CategorySpec> fromKey(const QString& key);
    bool operator==(const CategorySpec& o) const { return type == o.type && arg == o.arg; }
};

// One artist of an album (the name under the cover is a link per artist).
struct OwnerLink {
    qint64 id = 0;
    QString name;
};

// An album or playlist in a carousel or tile list.
struct CarouselItem {
    ContainerKind kind = ContainerKind::Album;
    qint64 id = 0;
    QString title;
    QString subtitle;   // albums: "Artist, Artist"; playlists: empty
    QString cover;      // library-relative, empty = placeholder
    qint64 ownerId = 0; // albums: primary artist
    QList<OwnerLink> owners;  // albums: all artists in order (primary first)
    int year = 0;
    int albumType = 0;  // AlbumType as int (albums)
};

// Everything the category page shows.
struct CategoryContents {
    CategorySpec spec;
    QList<CarouselItem> items;
    qint64 durationMs = 0;  // all tracks of all items
    QString requestedKey;   // the key this was asked for ("recent" has no spec of its own)
    QString sectionLabel;   // an artist's carousel ("section:<id>:<name>"): the artist name; the view-model builds "Name: Albums"
};

}  // namespace semplice
