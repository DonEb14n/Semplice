#pragma once

#include "core/db/Database.h"
#include "core/domain/ReleaseDate.h"
#include "core/home/CategoryTypes.h"

namespace semplice {

struct ArtistRef {
    qint64 id = 0;
    QString name;
    bool hidden = false;
    QString avatar;  // library-relative
    bool userHidden = false;  // hidden by hand (grey, left out of the library list)
};

// One line of a track table (album, playlist, queue of a card).
struct TrackRow {
    qint64 id = 0;
    int position = 0;
    QString title;
    QList<ArtistRef> artists;     // credits in order
    QString albumTitle;           // the album the track belongs to (playlist table, "Album name" column); empty = none
    qint64 albumId = 0;
    qint64 durationMs = 0;
    bool hasEmbeddedCover = false;
    QString coverPath;            // album cover, library-relative
    QString filePath;             // library-relative (to read the embedded picture)
    int memberships = 1;          // containers holding the track; "Remove" needs more than one (UI p.38)
    bool inPlaylist = false;      // lives in a playlist now
    QString artistsText() const;
};

// The kinds of carousel on an artist card, in the order of UI p.34-35.
enum class ArtistSection { Discography, Albums, Singles, Eps, Live, Compilations, Other, Collaborations, Feats };

struct ArtistCardSection {
    ArtistSection section = ArtistSection::Discography;
    QList<CarouselItem> items;  // the carousel shows up to 50
    int total = 0;
};

struct ArtistCard {
    ArtistRef artist;
    QStringList tags;
    // statistics of authored albums by type; types without albums are not shown
    int albums = 0, singles = 0, eps = 0, lives = 0, compilations = 0, others = 0;
    QList<ArtistCardSection> sections;  // only the non-empty ones
    bool hasUnsorted = false;
};

struct AlbumCard {
    qint64 id = 0;                 // negative: the virtual "Unsorted tracks" of a hidden artist (-artist id)
    QString title;
    bool isUnsorted = false;
    AlbumType type = AlbumType::Album;
    ReleaseDate date;
    QString cover;
    QList<ArtistRef> artists;      // primary first, then the co-authors
    QStringList tags;
    QList<TrackRow> tracks;
    qint64 durationMs = 0;
    qint64 createdAt = 0;
};

struct PlaylistCard {
    qint64 id = 0;
    QString name;
    QString cover;
    QList<TrackRow> tracks;
    qint64 durationMs = 0;
};

// Read-only questions behind the cards (engine thread).
class CardQueries {
public:
    explicit CardQueries(Database& db) : m_db(db) {}

    Result<ArtistCard> artist(ArtistId id);
    // id < 0: the virtual Unsorted tracks album of the hidden artist -id.
    Result<AlbumCard> album(qint64 id);
    Result<PlaylistCard> playlist(PlaylistId id);
    // Items of one artist section (all of them when limit == 0).
    Result<QList<CarouselItem>> section(ArtistId artist, ArtistSection section, int limit);
    Result<QList<ArtistRef>> artists(const QString& query, int limit, bool includeHidden = true);  // pickers
    Result<TrackRow> track(TrackId id);

    static QString sectionKey(ArtistId artist, ArtistSection section);
    static std::optional<std::pair<ArtistId, ArtistSection>> parseSectionKey(const QString& key);

private:
    Result<QList<TrackRow>> rows(const QString& tracksSql, const QVariantList& args);
    Database& m_db;
};

}  // namespace semplice
