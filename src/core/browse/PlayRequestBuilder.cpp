#include "core/browse/PlayRequestBuilder.h"

#include "core/db/AlbumRepo.h"
#include "core/db/ArtistRepo.h"
#include "core/db/PlaylistRepo.h"
#include "core/db/TrackRepo.h"

namespace semplice {

Result<QString> PlayRequestBuilder::artistName(ArtistId artist)
{
    ArtistRepo repo(m_db);
    auto row = repo.get(artist);
    if (!row.ok())
        return row.error();
    if (!row.value())
        return Error::make(ErrorCode::NotFound, QStringLiteral("artist"));
    return row.value()->name;
}

Result<PlayRequest> PlayRequestBuilder::discography(ArtistId artist)
{
    auto name = artistName(artist);
    if (!name.ok())
        return name.error();
    auto rows = m_db.run(
        QStringLiteral("SELECT a.id FROM album a WHERE a.primary_artist_id = ? "
                       "OR a.id IN (SELECT album_id FROM album_artist WHERE artist_id = ?) "
                       "ORDER BY a.year DESC, COALESCE(a.month, 0) DESC, COALESCE(a.day, 0) DESC, a.created_at DESC, a.id DESC"),
        {artist.value, artist.value});
    if (!rows.ok())
        return rows.error();
    QList<AlbumId> albums;
    QSqlQuery q = std::move(rows).value();
    while (q.next())
        albums.append(AlbumId(q.value(0).toLongLong()));

    AlbumRepo albumRepo(m_db);
    QueueGroup group;
    group.origin = {OriginKind::Discography, artist.value, name.value()};
    for (AlbumId album : albums) {
        auto tracks = albumRepo.tracks(album);
        if (!tracks.ok())
            return tracks.error();
        for (TrackId t : tracks.value())
            group.tracks.append({t, album.value, ContainerKind::Album});
    }
    PlayRequest request;
    request.groups = {group};
    request.afterEnd = AfterEnd::Recommend;
    return request;
}

Result<PlayRequest> PlayRequestBuilder::hiddenArtist(ArtistId artist)
{
    auto name = artistName(artist);
    if (!name.ok())
        return name.error();
    TrackRepo tracks(m_db);
    auto ids = tracks.unsortedOfHiddenArtist(artist);
    if (!ids.ok())
        return ids.error();
    QueueGroup group;
    group.origin = {OriginKind::Discography, artist.value, name.value()};
    for (TrackId t : ids.value())
        group.tracks.append({t});
    PlayRequest request;
    request.groups = {group};
    request.afterEnd = AfterEnd::Recommend;
    return request;
}

Result<PlayRequest> PlayRequestBuilder::album(AlbumId id)
{
    AlbumRepo albums(m_db);
    auto row = albums.get(id);
    if (!row.ok())
        return row.error();
    if (!row.value())
        return Error::make(ErrorCode::NotFound, QStringLiteral("album"));
    auto tracks = albums.tracks(id);
    if (!tracks.ok())
        return tracks.error();
    QueueGroup group;
    group.origin = {OriginKind::Album, id.value, row.value()->title};
    for (TrackId t : tracks.value())
        group.tracks.append({t});
    PlayRequest request;
    request.groups = {group};
    request.afterEnd = AfterEnd::Recommend;
    return request;
}

Result<PlayRequest> PlayRequestBuilder::playlist(PlaylistId id)
{
    PlaylistRepo playlists(m_db);
    auto row = playlists.get(id);
    if (!row.ok())
        return row.error();
    if (!row.value())
        return Error::make(ErrorCode::NotFound, QStringLiteral("playlist"));
    auto tracks = playlists.tracks(id);
    if (!tracks.ok())
        return tracks.error();
    QueueGroup group;
    group.origin = {OriginKind::Playlist, id.value, row.value()->name};
    for (TrackId t : tracks.value())
        group.tracks.append({t});
    PlayRequest request;
    request.groups = {group};
    request.afterEnd = AfterEnd::Stop;
    return request;
}

Result<PlayRequest> PlayRequestBuilder::singleTrack(TrackId id)
{
    TrackRepo tracks(m_db);
    auto row = tracks.get(id);
    if (!row.ok())
        return row.error();
    if (!row.value())
        return Error::make(ErrorCode::NotFound, QStringLiteral("track"));
    QueueGroup group;
    const Track& t = *row.value();
    QString title;
    if (t.home.kind == ContainerKind::Album) {
        if (auto a = AlbumRepo(m_db).get(AlbumId(t.home.id)); a.ok() && a.value())
            title = a.value()->title;
        group.origin = {OriginKind::Album, t.home.id, title};
    } else {
        if (auto p = PlaylistRepo(m_db).get(PlaylistId(t.home.id)); p.ok() && p.value())
            title = p.value()->name;
        group.origin = {OriginKind::Playlist, t.home.id, title};
    }
    group.tracks = {{id}};
    PlayRequest request;
    request.groups = {group};
    request.afterEnd = AfterEnd::Stop;
    return request;
}

}  // namespace semplice

namespace semplice {

Result<PlayRequest> PlayRequestBuilder::carouselFrom(const QList<ContainerRef>& items, int startIndex)
{
    PlayRequest request;
    request.afterEnd = AfterEnd::Recommend;
    for (int i = 0; i < items.size(); ++i) {
        auto one = items.at(i).kind == ContainerKind::Album ? album(AlbumId(items.at(i).id)) : playlist(PlaylistId(items.at(i).id));
        if (!one.ok() || one.value().groups.isEmpty())
            continue;
        if (i <= startIndex)
            request.startGroup = static_cast<int>(request.groups.size());
        request.groups.append(one.value().groups.first());
    }
    if (request.groups.isEmpty())
        return Error::make(ErrorCode::NotFound, QStringLiteral("carousel"));
    return request;
}

Result<PlayRequest> PlayRequestBuilder::carouselAll(const QList<ContainerRef>& items, const QString& title, const QString& key)
{
    QueueGroup group;
    group.origin = {OriginKind::Carousel, 0, title, key};
    for (const ContainerRef& ref : items) {
        auto one = ref.kind == ContainerKind::Album ? album(AlbumId(ref.id)) : playlist(PlaylistId(ref.id));
        if (!one.ok() || one.value().groups.isEmpty())
            continue;
        for (const QueueTrack& t : one.value().groups.first().tracks)
            group.tracks.append({t.track, ref.id, ref.kind});
    }
    if (group.tracks.isEmpty())
        return Error::make(ErrorCode::NotFound, QStringLiteral("carousel"));
    PlayRequest request;
    request.groups = {group};
    request.afterEnd = AfterEnd::Recommend;
    return request;
}

}  // namespace semplice
