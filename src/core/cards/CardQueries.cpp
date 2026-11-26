#include "core/cards/CardQueries.h"

#include "core/db/AlbumRepo.h"
#include "core/db/ArtistRepo.h"
#include "core/db/PlaylistRepo.h"
#include "core/db/TagRepo.h"
#include "core/db/TrackRepo.h"
#include "core/home/HomeQueries.h"

#include <QDateTime>

#include <algorithm>

namespace semplice {

QString TrackRow::artistsText() const
{
    QStringList names;
    for (const ArtistRef& a : artists)
        names << a.name;
    return names.join(QStringLiteral(", "));
}

namespace {

const QString kNewestFirst = QStringLiteral("a.year DESC, COALESCE(a.month, 0) DESC, COALESCE(a.day, 0) DESC, a.created_at DESC, a.id DESC");

QString authoredWhere()
{
    return QStringLiteral("(a.primary_artist_id = ? OR a.id IN (SELECT album_id FROM album_artist WHERE artist_id = ?))");
}

QString sectionName(ArtistSection s)
{
    switch (s) {
    case ArtistSection::Discography: return QStringLiteral("discography");
    case ArtistSection::Albums: return QStringLiteral("albums");
    case ArtistSection::Singles: return QStringLiteral("singles");
    case ArtistSection::Eps: return QStringLiteral("eps");
    case ArtistSection::Live: return QStringLiteral("live");
    case ArtistSection::Compilations: return QStringLiteral("compilations");
    case ArtistSection::Other: return QStringLiteral("other");
    case ArtistSection::Collaborations: return QStringLiteral("collaborations");
    case ArtistSection::Feats: return QStringLiteral("feats");
    }
    return {};
}

}  // namespace

QString CardQueries::sectionKey(ArtistId artist, ArtistSection section)
{
    return QStringLiteral("section:%1:%2").arg(artist.value).arg(sectionName(section));
}

std::optional<std::pair<ArtistId, ArtistSection>> CardQueries::parseSectionKey(const QString& key)
{
    const QStringList parts = key.split(QLatin1Char(':'));
    if (parts.size() != 3 || parts.at(0) != QLatin1String("section"))
        return std::nullopt;
    bool ok = false;
    const qint64 id = parts.at(1).toLongLong(&ok);
    if (!ok || id <= 0)
        return std::nullopt;
    for (int i = 0; i <= static_cast<int>(ArtistSection::Feats); ++i)
        if (sectionName(static_cast<ArtistSection>(i)) == parts.at(2))
            return std::make_pair(ArtistId(id), static_cast<ArtistSection>(i));
    return std::nullopt;
}

Result<TrackRow> CardQueries::track(TrackId id)
{
    TrackRepo tracks(m_db);
    ArtistRepo artists(m_db);
    auto t = tracks.get(id);
    if (!t.ok())
        return t.error();
    if (!t.value())
        return Error::make(ErrorCode::NotFound, QStringLiteral("track"));
    TrackRow row;
    row.id = id.value;
    row.title = t.value()->title;
    row.durationMs = t.value()->durationMs;
    row.hasEmbeddedCover = t.value()->hasEmbeddedCover;
    row.filePath = t.value()->filePath;
    if (auto credits = tracks.credits(id); credits.ok()) {
        for (const TrackCredit& c : credits.value())
            if (auto a = artists.get(c.artist); a.ok() && a.value())
                row.artists.append({a.value()->id.value, a.value()->name, a.value()->hidden, a.value()->avatarPath});
    }
    if (auto n = tracks.membershipCount(id); n.ok())
        row.memberships = n.value();
    if (auto ms = tracks.memberships(id); ms.ok()) {
        AlbumRepo albums(m_db);
        for (const Membership& m : ms.value()) {
            if (m.kind == ContainerKind::Playlist) {
                row.inPlaylist = true;
                continue;
            }
            if (row.albumId == 0) {
                if (auto a = albums.get(AlbumId(m.containerId)); a.ok() && a.value() && !a.value()->isUnsorted) {
                    row.albumId = a.value()->id.value;
                    row.albumTitle = a.value()->title;
                    row.coverPath = a.value()->coverPath;
                }
            }
        }
    }
    return row;
}

Result<QList<TrackRow>> CardQueries::rows(const QString& sql, const QVariantList& args)
{
    auto q = m_db.run(sql, args);
    if (!q.ok())
        return q.error();
    QList<QPair<qint64, int>> ids;
    QSqlQuery query = std::move(q).value();
    while (query.next())
        ids.append({query.value(0).toLongLong(), query.value(1).toInt()});
    QList<TrackRow> out;
    for (const auto& id : std::as_const(ids)) {
        auto row = track(TrackId(id.first));
        if (!row.ok())
            continue;
        row.value().position = id.second;
        out.append(row.value());
    }
    return out;
}

Result<AlbumCard> CardQueries::album(qint64 id)
{
    AlbumCard card;
    ArtistRepo artists(m_db);
    TagRepo tags(m_db);
    if (id < 0) {  // the virtual "Unsorted tracks" of a hidden artist
        const ArtistId owner(-id);
        auto a = artists.get(owner);
        if (!a.ok())
            return a.error();
        if (!a.value() || !a.value()->hidden)
            return Error::make(ErrorCode::NotFound, QStringLiteral("hidden artist"));
        card.id = id;
        card.isUnsorted = true;
        card.type = AlbumType::Other;
        card.artists = {{a.value()->id.value, a.value()->name, true, {}}};
        card.createdAt = a.value()->createdAt;
        TrackRepo tracks(m_db);
        auto ids = tracks.unsortedOfHiddenArtist(owner);
        if (!ids.ok())
            return ids.error();
        int pos = 0;
        for (TrackId t : ids.value()) {
            auto row = track(t);
            if (row.ok()) {
                row.value().position = ++pos;
                card.durationMs += row.value().durationMs;
                card.tracks.append(row.value());
            }
        }
        return card;
    }
    AlbumRepo albums(m_db);
    auto a = albums.get(AlbumId(id));
    if (!a.ok())
        return a.error();
    if (!a.value())
        return Error::make(ErrorCode::NotFound, QStringLiteral("album"));
    const Album& al = *a.value();
    card.id = id;
    card.title = al.title;
    card.isUnsorted = al.isUnsorted;
    card.type = al.type;
    card.date = al.date;
    card.cover = al.coverPath;
    card.createdAt = al.createdAt;
    if (auto p = artists.get(al.primaryArtist); p.ok() && p.value())
        card.artists.append({p.value()->id.value, p.value()->name, p.value()->hidden, p.value()->avatarPath});
    if (auto co = albums.coAuthors(AlbumId(id)); co.ok())
        for (const CoAuthor& c : co.value())
            if (auto p = artists.get(c.artist); p.ok() && p.value())
                card.artists.append({p.value()->id.value, p.value()->name, p.value()->hidden, p.value()->avatarPath});
    if (auto t = tags.tags(TagOwner::Album, id); t.ok())
        card.tags = t.value();
    auto rowsResult = rows(QStringLiteral("SELECT track_id, position FROM album_track WHERE album_id = ? ORDER BY position"), {id});
    if (!rowsResult.ok())
        return rowsResult.error();
    card.tracks = rowsResult.value();
    for (const TrackRow& r : std::as_const(card.tracks))
        card.durationMs += r.durationMs;
    return card;
}

Result<PlaylistCard> CardQueries::playlist(PlaylistId id)
{
    PlaylistRepo playlists(m_db);
    auto p = playlists.get(id);
    if (!p.ok())
        return p.error();
    if (!p.value())
        return Error::make(ErrorCode::NotFound, QStringLiteral("playlist"));
    PlaylistCard card;
    card.id = id.value;
    card.name = p.value()->name;
    card.cover = p.value()->coverPath;
    auto rowsResult = rows(QStringLiteral("SELECT track_id, position FROM playlist_track WHERE playlist_id = ? ORDER BY position"), {id.value});
    if (!rowsResult.ok())
        return rowsResult.error();
    card.tracks = rowsResult.value();
    for (const TrackRow& r : std::as_const(card.tracks))
        card.durationMs += r.durationMs;
    return card;
}

Result<QList<CarouselItem>> CardQueries::section(ArtistId artist, ArtistSection s, int limit)
{
    HomeQueries home(m_db);
    QString where;
    QVariantList args;
    const QString authored = QStringLiteral("a.is_unsorted = 0 AND ") + authoredWhere();
    auto typed = [&](AlbumType t) {
        where = authored + QStringLiteral(" AND a.type = '%1'").arg(toDbString(t));
        args = {artist.value, artist.value};
    };
    switch (s) {
    case ArtistSection::Discography:
        // authored albums, the real Unsorted album of a visible artist included (it appears only here)
        where = QStringLiteral("(") + authoredWhere() + QStringLiteral(")");
        args = {artist.value, artist.value};
        break;
    case ArtistSection::Albums: typed(AlbumType::Album); break;
    case ArtistSection::Singles: typed(AlbumType::Single); break;
    case ArtistSection::Eps: typed(AlbumType::Ep); break;
    case ArtistSection::Live: typed(AlbumType::Live); break;
    case ArtistSection::Compilations: typed(AlbumType::Compilation); break;
    case ArtistSection::Other: typed(AlbumType::Other); break;
    case ArtistSection::Collaborations:
        where = QStringLiteral("a.is_unsorted = 0 AND a.primary_artist_id != ? AND a.id IN (SELECT album_id FROM album_artist WHERE artist_id = ?)");
        args = {artist.value, artist.value};
        break;
    case ArtistSection::Feats:
        where = QStringLiteral("a.is_unsorted = 0 AND a.primary_artist_id != ? AND a.id NOT IN (SELECT album_id FROM album_artist WHERE artist_id = ?) "
                               "AND a.id IN (SELECT atr.album_id FROM album_track atr JOIN track_artist ta ON ta.track_id = atr.track_id WHERE ta.artist_id = ?)");
        args = {artist.value, artist.value, artist.value};
        break;
    }
    auto items = home.albums(where, args, kNewestFirst);
    if (!items.ok())
        return items;
    if (limit > 0 && items.value().size() > limit)
        items.value().resize(limit);
    return items;
}

Result<ArtistCard> CardQueries::artist(ArtistId id)
{
    ArtistRepo artists(m_db);
    auto a = artists.get(id);
    if (!a.ok())
        return a.error();
    if (!a.value())
        return Error::make(ErrorCode::NotFound, QStringLiteral("artist"));
    ArtistCard card;
    card.artist = {id.value, a.value()->name, a.value()->hidden, a.value()->avatarPath, a.value()->userHidden};
    if (auto t = TagRepo(m_db).tags(TagOwner::Artist, id.value); t.ok())
        card.tags = t.value();

    if (!card.artist.hidden) {
        auto rowsResult = m_db.run(QStringLiteral("SELECT a.type, COUNT(*) FROM album a WHERE a.is_unsorted = 0 AND ") + authoredWhere() +
                                       QStringLiteral(" GROUP BY a.type"),
                                   {id.value, id.value});
        if (rowsResult.ok()) {
            QSqlQuery q = std::move(rowsResult).value();
            while (q.next()) {
                const int n = q.value(1).toInt();
                switch (albumTypeFromString(q.value(0).toString()).value_or(AlbumType::Other)) {
                case AlbumType::Album: card.albums = n; break;
                case AlbumType::Single: card.singles = n; break;
                case AlbumType::Ep: card.eps = n; break;
                case AlbumType::Live: card.lives = n; break;
                case AlbumType::Compilation: card.compilations = n; break;
                case AlbumType::Other: card.others = n; break;
                }
            }
        }
    }

    auto add = [&](ArtistSection s, QList<CarouselItem> items) {
        if (items.isEmpty())
            return;
        ArtistCardSection section;
        section.section = s;
        section.total = static_cast<int>(items.size());
        if (items.size() > 50)
            items.resize(50);
        section.items = std::move(items);
        card.sections.append(section);
    };

    if (card.artist.hidden) {
        // the virtual "Unsorted tracks" album is the whole discography of a hidden artist
        auto virtualAlbum = album(-id.value);
        if (virtualAlbum.ok() && !virtualAlbum.value().tracks.isEmpty()) {
            CarouselItem item;
            item.kind = ContainerKind::Album;
            item.id = -id.value;
            item.albumType = static_cast<int>(AlbumType::Other);
            item.year = QDateTime::fromMSecsSinceEpoch(a.value()->createdAt).date().year();
            item.ownerId = id.value;
            add(ArtistSection::Discography, {item});
        }
    } else {
        for (ArtistSection s : {ArtistSection::Discography, ArtistSection::Albums, ArtistSection::Singles, ArtistSection::Eps, ArtistSection::Live,
                                ArtistSection::Compilations, ArtistSection::Other, ArtistSection::Collaborations}) {
            auto items = section(id, s, 0);
            if (items.ok())
                add(s, items.value());
        }
        if (auto r = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM album WHERE is_unsorted = 1 AND primary_artist_id = ?"), {id.value}); r.ok())
            card.hasUnsorted = r.value().toInt() > 0;
    }
    if (auto items = section(id, ArtistSection::Feats, 0); items.ok())
        add(ArtistSection::Feats, items.value());
    return card;
}

Result<QList<ArtistRef>> CardQueries::artists(const QString& query, int limit, bool includeHidden)
{
    ArtistRepo repo(m_db);
    auto all = repo.all();
    if (!all.ok())
        return all.error();
    const QString needle = query.trimmed().toCaseFolded();
    QList<ArtistRef> out;
    QList<Artist> list = all.value();
    std::sort(list.begin(), list.end(), [](const Artist& x, const Artist& y) { return x.name.compare(y.name, Qt::CaseInsensitive) < 0; });
    for (const Artist& a : std::as_const(list)) {
        if (!includeHidden && a.hidden)
            continue;
        if (!needle.isEmpty() && !a.name.toCaseFolded().contains(needle))
            continue;
        out.append({a.id.value, a.name, a.hidden, a.avatarPath, a.userHidden});
        if (limit > 0 && out.size() >= limit)
            break;
    }
    return out;
}

}  // namespace semplice
