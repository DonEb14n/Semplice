#pragma once

#include "core/domain/Types.h"

#include <QList>
#include <QString>

namespace semplice {

enum class OriginKind { Album, Playlist, Discography, Carousel, Recommendations };
enum class RepeatMode { Off, All, One };
enum class AfterEnd { Stop, Recommend };
enum class PlayState { Stopped, Playing, Paused };

// Where a queue group comes from (player header, Recently Played).
struct QueueOrigin {
    OriginKind kind = OriginKind::Album;
    qint64 id = 0;       // album / playlist / artist (discography) / carousel-item id; 0 for recommendations
    QString title;
    QString key = QString();  // Carousel: the category key, so the player header can open the category page
    bool operator==(const QueueOrigin&) const = default;
};

struct QueueTrack {
    TrackId track;
    // The album / playlist this entry comes from, when the group mixes several (Discography, Carousel):
    // that container is what Recently Played registers (each album separately). 0 = use the group origin.
    qint64 sourceId = 0;
    ContainerKind sourceKind = ContainerKind::Album;
    bool operator==(const QueueTrack&) const = default;
};

struct QueueGroup {
    QueueOrigin origin;
    QList<QueueTrack> tracks;
};

// What the play buttons hand to the service (UI doc "PLAYBACK RULES", SPEC 7.2).
struct PlayRequest {
    QList<QueueGroup> groups;
    int startGroup = 0;
    int startIndex = -1;                 // -1: first track (random one when shuffle is on)
    AfterEnd afterEnd = AfterEnd::Stop;  // Recommend: album / artist / carousel windows; Stop: playlists and search
};

struct QueuePosition {
    int group = -1;
    int index = -1;  // index into QueueGroup::tracks (not into the play order)
    bool valid() const { return group >= 0 && index >= 0; }
    bool operator==(const QueuePosition&) const = default;
};

}  // namespace semplice
