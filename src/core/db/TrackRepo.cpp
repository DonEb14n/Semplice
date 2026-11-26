#include "core/db/TrackRepo.h"

namespace semplice {

namespace {

const QString kColumns = QStringLiteral(
    "id, title, duration_ms, file_path, file_size, file_mtime, codec, content_hash, has_embedded_cover, "
    "play_count, last_played_at, home_kind, home_id, created_at, updated_at, tags_dirty");

Track readTrack(QSqlQuery& q)
{
    Track t;
    t.id = TrackId(q.value(0).toLongLong());
    t.title = q.value(1).toString();
    t.durationMs = q.value(2).toLongLong();
    t.filePath = q.value(3).toString();
    t.fileSize = q.value(4).toLongLong();
    t.fileMtime = q.value(5).toLongLong();
    t.codec = q.value(6).toString();
    t.contentHash = q.value(7).toByteArray();
    t.hasEmbeddedCover = q.value(8).toInt() != 0;
    t.playCount = q.value(9).toInt();
    if (!q.value(10).isNull())
        t.lastPlayedAt = q.value(10).toLongLong();
    t.home.kind = q.value(11).toString() == QLatin1String("album") ? ContainerKind::Album : ContainerKind::Playlist;
    t.home.id = q.value(12).toLongLong();
    t.createdAt = q.value(13).toLongLong();
    t.updatedAt = q.value(14).toLongLong();
    t.tagsDirty = q.value(15).toInt() != 0;
    return t;
}

Result<std::optional<Track>> readOne(Result<QSqlQuery> r)
{
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    if (!q.next())
        return std::optional<Track>();
    return std::optional<Track>(readTrack(q));
}

Result<QList<TrackId>> readIds(Result<QSqlQuery> r)
{
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<TrackId> out;
    while (q.next())
        out.append(TrackId(q.value(0).toLongLong()));
    return out;
}

}  // namespace

Result<TrackId> TrackRepo::insert(const Track& t)
{
    auto r = m_db.exec(
        QStringLiteral("INSERT INTO track(title, duration_ms, file_path, file_size, file_mtime, codec, content_hash, "
                       "has_embedded_cover, play_count, last_played_at, home_kind, home_id, created_at, updated_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
        {t.title.trimmed(), t.durationMs, t.filePath, t.fileSize, t.fileMtime,
         t.codec.isEmpty() ? QVariant() : QVariant(t.codec), t.contentHash, t.hasEmbeddedCover ? 1 : 0, t.playCount,
         t.lastPlayedAt ? QVariant(*t.lastPlayedAt) : QVariant(), toDbString(t.home.kind), t.home.id, t.createdAt,
         t.updatedAt});
    if (!r.ok())
        return r.error();
    return TrackId(m_db.lastInsertId());
}

Result<std::optional<Track>> TrackRepo::get(TrackId id)
{
    return readOne(m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM track WHERE id = ?"),
                            {id.value}));
}

Result<std::optional<Track>> TrackRepo::findByHash(const QByteArray& hash)
{
    return readOne(m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM track WHERE content_hash = ?"),
                            {hash}));
}

Result<std::optional<Track>> TrackRepo::findByPath(const QString& relativePath)
{
    return readOne(m_db.run(QStringLiteral("SELECT ") + kColumns +
                                QStringLiteral(" FROM track WHERE file_path = ? COLLATE NOCASE"),
                            {relativePath}));
}

Result<bool> TrackRepo::pathTaken(const QString& relativePath)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM track WHERE file_path = ? COLLATE NOCASE"),
                         {relativePath});
    if (!v.ok())
        return v.error();
    return v.value().toInt() > 0;
}

Result<void> TrackRepo::setHome(TrackId id, HomeRef home, const QString& filePath, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE track SET home_kind = ?, home_id = ?, file_path = ?, updated_at = ? "
                                    "WHERE id = ?"),
                     {toDbString(home.kind), home.id, filePath, now, id.value});
}

Result<void> TrackRepo::remove(TrackId id)
{
    return m_db.exec(QStringLiteral("DELETE FROM track WHERE id = ?"), {id.value});
}

Result<QList<TrackCredit>> TrackRepo::credits(TrackId id)
{
    auto r = m_db.run(QStringLiteral("SELECT artist_id, position, role FROM track_artist WHERE track_id = ? ORDER BY "
                                     "position"),
                      {id.value});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<TrackCredit> out;
    while (q.next())
        out.append({ArtistId(q.value(0).toLongLong()), q.value(1).toInt(),
                    creditRoleFromString(q.value(2).toString()).value_or(CreditRole::Main)});
    return out;
}

Result<void> TrackRepo::setCredits(TrackId id, const QList<TrackCredit>& credits)
{
    if (auto r = m_db.exec(QStringLiteral("DELETE FROM track_artist WHERE track_id = ?"), {id.value}); !r.ok())
        return r.error();
    for (const TrackCredit& c : credits) {
        if (auto r = m_db.exec(QStringLiteral("INSERT OR IGNORE INTO track_artist(track_id, artist_id, position, role) "
                                              "VALUES(?,?,?,?)"),
                               {id.value, c.artist.value, c.position, toDbString(c.role)});
            !r.ok())
            return r.error();
    }
    return {};
}

Result<void> TrackRepo::addCredit(TrackId id, TrackCredit credit)
{
    auto next = m_db.scalar(QStringLiteral("SELECT COALESCE(MAX(position) + 1, 0) FROM track_artist WHERE track_id = ?"),
                            {id.value});
    if (!next.ok())
        return next.error();
    return m_db.exec(QStringLiteral("INSERT OR IGNORE INTO track_artist(track_id, artist_id, position, role) "
                                    "VALUES(?,?,?,?)"),
                     {id.value, credit.artist.value, next.value().toInt(), toDbString(credit.role)});
}

Result<QList<Membership>> TrackRepo::memberships(TrackId id)
{
    auto r = m_db.run(QStringLiteral("SELECT 'album', album_id, position, added_at FROM album_track WHERE track_id = ? "
                                     "UNION ALL SELECT 'playlist', playlist_id, position, added_at FROM "
                                     "playlist_track WHERE track_id = ? ORDER BY 4, 1, 2"),
                      {id.value, id.value});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<Membership> out;
    while (q.next())
        out.append({q.value(0).toString() == QLatin1String("album") ? ContainerKind::Album : ContainerKind::Playlist,
                    q.value(1).toLongLong(), q.value(2).toInt(), q.value(3).toLongLong()});
    return out;
}

Result<int> TrackRepo::membershipCount(TrackId id)
{
    auto v = m_db.scalar(QStringLiteral("SELECT (SELECT COUNT(*) FROM album_track WHERE track_id = ?) + (SELECT "
                                        "COUNT(*) FROM playlist_track WHERE track_id = ?)"),
                         {id.value, id.value});
    if (!v.ok())
        return v.error();
    return v.value().toInt();
}

Result<int> TrackRepo::creditCount(ArtistId artist)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM track_artist WHERE artist_id = ?"), {artist.value});
    if (!v.ok())
        return v.error();
    return v.value().toInt();
}

Result<QList<TrackId>> TrackRepo::creditedTo(ArtistId artist)
{
    return readIds(m_db.run(QStringLiteral("SELECT track_id FROM track_artist WHERE artist_id = ? ORDER BY track_id"),
                            {artist.value}));
}

Result<QList<TrackId>> TrackRepo::unsortedOfHiddenArtist(ArtistId artist)
{
    return readIds(m_db.run(QStringLiteral("SELECT DISTINCT ta.track_id FROM track_artist ta WHERE ta.artist_id = ? "
                                           "AND ta.role = 'main' AND EXISTS (SELECT 1 FROM playlist_track pt WHERE "
                                           "pt.track_id = ta.track_id) ORDER BY ta.track_id"),
                            {artist.value}));
}

Result<void> TrackRepo::recordPlay(TrackId id, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE track SET play_count = play_count + 1, last_played_at = ? WHERE id = ?"),
                     {now, id.value});
}

Result<void> TrackRepo::setTagsDirty(TrackId id, bool dirty)
{
    return m_db.exec(QStringLiteral("UPDATE track SET tags_dirty = ? WHERE id = ?"), {dirty ? 1 : 0, id.value});
}

Result<QList<TrackId>> TrackRepo::dirtyTracks()
{
    return readIds(m_db.run(QStringLiteral("SELECT id FROM track WHERE tags_dirty = 1 ORDER BY id")));
}

Result<void> TrackRepo::setFileInfo(TrackId id, qint64 size, qint64 mtime, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE track SET file_size = ?, file_mtime = ?, updated_at = ? WHERE id = ?"),
                     {size, mtime, now, id.value});
}

Result<void> TrackRepo::setMedia(TrackId id, qint64 durationMs, const QString& codec, bool hasEmbeddedCover)
{
    return m_db.exec(QStringLiteral("UPDATE track SET duration_ms = ?, codec = ?, has_embedded_cover = ? WHERE id = ?"),
                     {durationMs, codec.isEmpty() ? QVariant() : QVariant(codec), hasEmbeddedCover ? 1 : 0, id.value});
}

Result<void> TrackRepo::setTitle(TrackId id, const QString& title, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE track SET title = ?, updated_at = ? WHERE id = ?"), {title.trimmed(), now, id.value});
}

Result<QList<TrackRepo::FileRow>> TrackRepo::allFiles()
{
    auto r = m_db.run(QStringLiteral("SELECT id, file_path, file_size, file_mtime FROM track ORDER BY id"));
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<FileRow> out;
    while (q.next())
        out.append({TrackId(q.value(0).toLongLong()), q.value(1).toString(), q.value(2).toLongLong(), q.value(3).toLongLong()});
    return out;
}

Result<int> TrackRepo::count()
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM track"));
    if (!v.ok())
        return v.error();
    return v.value().toInt();
}

}  // namespace semplice
