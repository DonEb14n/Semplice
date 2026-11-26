#pragma once

#include "core/db/Database.h"
#include "core/playback/PlaybackTypes.h"

namespace semplice {

// Turns "play this" clicks into queue requests (UI doc "PLAYBACK RULES", SPEC 7.2).
// An album or playlist chosen from a carousel or tile list.
struct ContainerRef {
    ContainerKind kind = ContainerKind::Album;
    qint64 id = 0;
    bool operator==(const ContainerRef&) const = default;
};

class PlayRequestBuilder {
public:
    explicit PlayRequestBuilder(Database& db) : m_db(db) {}

    // The artist's whole discography (albums they author or co-author), newest release first, tracks in album
    // order; one group, every entry tagged with its album so each is registered in Recently Played. The queue
    // continues with recommendations. Shuffle is applied by the player to the group.
    Result<PlayRequest> discography(ArtistId artist);
    // A hidden artist's tracks (their virtual "Unsorted tracks" album).
    Result<PlayRequest> hiddenArtist(ArtistId artist);
    Result<PlayRequest> album(AlbumId album);       // one album, then recommendations
    Result<PlayRequest> playlist(PlaylistId playlist);  // one playlist, stops at the end
    // A single track chosen in the search results: a queue of just that track, no endless stream.
    Result<PlayRequest> singleTrack(TrackId track);

    // An item started from inside a carousel (not "play all"): one independent group per carousel item, so each
    // registers in Recently Played, begun at `startIndex`; the player can go back to earlier groups; after the
    // last group the recommendations start (UI doc p.53). Items that cannot be read are left out.
    Result<PlayRequest> carouselFrom(const QList<ContainerRef>& items, int startIndex);
    // The "play" button of a category page: ONE group named after the carousel, every track tagged with the
    // album or playlist it comes from.
    Result<PlayRequest> carouselAll(const QList<ContainerRef>& items, const QString& title, const QString& key);

private:
    Result<QString> artistName(ArtistId artist);
    Database& m_db;
};

}  // namespace semplice
