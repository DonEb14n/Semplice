#pragma once

#include <QHash>
#include <QtGlobal>

#include <compare>

namespace semplice {

// Strongly typed row ids so an ArtistId can never be passed where an AlbumId is expected.
template <class Tag>
struct Id {
    qint64 value = 0;  // 0 = no id

    constexpr Id() = default;
    constexpr explicit Id(qint64 v) : value(v) {}
    constexpr bool isValid() const { return value > 0; }
    constexpr auto operator<=>(const Id&) const = default;
};

template <class Tag>
inline size_t qHash(const Id<Tag>& id, size_t seed = 0) noexcept
{
    return ::qHash(id.value, seed);
}

struct ArtistTag;
struct AlbumTag;
struct TrackTag;
struct PlaylistTag;
using ArtistId = Id<ArtistTag>;
using AlbumId = Id<AlbumTag>;
using TrackId = Id<TrackTag>;
using PlaylistId = Id<PlaylistTag>;

}  // namespace semplice
