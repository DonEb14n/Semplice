#include "core/db/PlaylistRepo.h"

#include "core/util/Normalizer.h"

namespace semplice {

namespace {

const QString kColumns = QStringLiteral("id, name, dir_name, cover_path, created_at, updated_at");

Playlist readPlaylist(QSqlQuery& q)
{
    Playlist p;
    p.id = PlaylistId(q.value(0).toLongLong());
    p.name = q.value(1).toString();
    p.dirName = q.value(2).toString();
    p.coverPath = q.value(3).toString();
    p.createdAt = q.value(4).toLongLong();
    p.updatedAt = q.value(5).toLongLong();
    return p;
}

Result<std::optional<Playlist>> readOne(Result<QSqlQuery> r)
{
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    if (!q.next())
        return std::optional<Playlist>();
    return std::optional<Playlist>(readPlaylist(q));
}

}  // namespace

Result<PlaylistId> PlaylistRepo::insert(const Playlist& playlist)
{
    const QString name = Normalizer::display(playlist.name);
    auto r = m_db.exec(QStringLiteral("INSERT INTO playlist(name, name_key, dir_name, cover_path, created_at, "
                                      "updated_at) VALUES(?,?,?,?,?,?)"),
                       {name, Normalizer::identityKey(name), playlist.dirName,
                        playlist.coverPath.isEmpty() ? QVariant() : QVariant(playlist.coverPath), playlist.createdAt,
                        playlist.updatedAt});
    if (!r.ok())
        return r.error();
    return PlaylistId(m_db.lastInsertId());
}

Result<std::optional<Playlist>> PlaylistRepo::get(PlaylistId id)
{
    return readOne(m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM playlist WHERE id = ?"),
                            {id.value}));
}

Result<std::optional<Playlist>> PlaylistRepo::findByName(const QString& name)
{
    return readOne(m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM playlist WHERE name_key = ?"),
                            {Normalizer::identityKey(name)}));
}

Result<QList<Playlist>> PlaylistRepo::all()
{
    auto r = m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM playlist ORDER BY id"));
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<Playlist> out;
    while (q.next())
        out.append(readPlaylist(q));
    return out;
}

Result<void> PlaylistRepo::setCover(PlaylistId id, const QString& coverPath, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE playlist SET cover_path = ?, updated_at = ? WHERE id = ?"),
                     {coverPath.isEmpty() ? QVariant() : QVariant(coverPath), now, id.value});
}

Result<void> PlaylistRepo::setName(PlaylistId id, const QString& name, const QString& dirName, qint64 now)
{
    const QString display = Normalizer::display(name);
    return m_db.exec(QStringLiteral("UPDATE playlist SET name = ?, name_key = ?, dir_name = ?, updated_at = ? WHERE id = ?"),
                     {display, Normalizer::identityKey(display), dirName, now, id.value});
}

Result<void> PlaylistRepo::reorder(PlaylistId id, const QList<TrackId>& order)
{
    int position = 1;
    for (TrackId t : order) {
        if (auto r = m_db.exec(QStringLiteral("UPDATE playlist_track SET position = ? WHERE playlist_id = ? AND track_id = ?"),
                               {position++, id.value, t.value});
            !r.ok())
            return r.error();
    }
    return {};
}

Result<void> PlaylistRepo::remove(PlaylistId id)
{
    return m_db.exec(QStringLiteral("DELETE FROM playlist WHERE id = ?"), {id.value});
}

Result<bool> PlaylistRepo::dirNameTaken(const QString& dirName)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM playlist WHERE dir_name = ? COLLATE NOCASE"), {dirName});
    if (!v.ok())
        return v.error();
    return v.value().toInt() > 0;
}

Result<QList<TrackId>> PlaylistRepo::tracks(PlaylistId id)
{
    auto r = m_db.run(QStringLiteral("SELECT track_id FROM playlist_track WHERE playlist_id = ? ORDER BY position"),
                      {id.value});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<TrackId> out;
    while (q.next())
        out.append(TrackId(q.value(0).toLongLong()));
    return out;
}

Result<int> PlaylistRepo::trackCount(PlaylistId id)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM playlist_track WHERE playlist_id = ?"), {id.value});
    if (!v.ok())
        return v.error();
    return v.value().toInt();
}

Result<void> PlaylistRepo::addTrack(PlaylistId id, TrackId track, int position, qint64 addedAt)
{
    if (auto r = m_db.exec(QStringLiteral("UPDATE playlist_track SET position = position + 1 WHERE playlist_id = ? "
                                          "AND position >= ?"),
                           {id.value, position});
        !r.ok())
        return r.error();
    return m_db.exec(QStringLiteral("INSERT INTO playlist_track(playlist_id, track_id, position, added_at) "
                                    "VALUES(?,?,?,?)"),
                     {id.value, track.value, position, addedAt});
}

Result<void> PlaylistRepo::removeTrack(PlaylistId id, TrackId track)
{
    return m_db.exec(QStringLiteral("DELETE FROM playlist_track WHERE playlist_id = ? AND track_id = ?"),
                     {id.value, track.value});
}

Result<void> PlaylistRepo::renumber(PlaylistId id)
{
    // Row by row on purpose (see AlbumRepo::renumber).
    auto r = m_db.run(QStringLiteral("SELECT track_id, position FROM playlist_track WHERE playlist_id = ? ORDER BY "
                                     "position, track_id"),
                      {id.value});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<QPair<qint64, int>> rows;
    while (q.next())
        rows.append({q.value(0).toLongLong(), q.value(1).toInt()});
    int expected = 1;
    for (const auto& row : std::as_const(rows)) {
        if (row.second != expected) {
            if (auto u = m_db.exec(QStringLiteral("UPDATE playlist_track SET position = ? WHERE playlist_id = ? AND "
                                                  "track_id = ?"),
                                   {expected, id.value, row.first});
                !u.ok())
                return u.error();
        }
        ++expected;
    }
    return {};
}

Result<int> PlaylistRepo::positionOf(PlaylistId id, TrackId track)
{
    auto v = m_db.scalar(QStringLiteral("SELECT position FROM playlist_track WHERE playlist_id = ? AND track_id = ?"),
                         {id.value, track.value});
    if (!v.ok())
        return v.error();
    return v.value().isValid() ? v.value().toInt() : 0;
}

}  // namespace semplice
