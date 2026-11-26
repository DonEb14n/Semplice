#include "core/db/AlbumRepo.h"

#include "core/util/Normalizer.h"

namespace semplice {

namespace {

const QString kColumns = QStringLiteral(
    "id, primary_artist_id, title, dir_name, type, is_unsorted, year, month, day, cover_path, created_at, "
    "updated_at");

Album readAlbum(QSqlQuery& q)
{
    Album a;
    a.id = AlbumId(q.value(0).toLongLong());
    a.primaryArtist = ArtistId(q.value(1).toLongLong());
    a.title = q.value(2).toString();
    a.dirName = q.value(3).toString();
    a.type = albumTypeFromString(q.value(4).toString()).value_or(AlbumType::Other);
    a.isUnsorted = q.value(5).toInt() != 0;
    a.date.year = q.value(6).toInt();
    a.date.month = q.value(7).toInt();  // NULL -> 0
    a.date.day = q.value(8).toInt();
    a.coverPath = q.value(9).toString();
    a.createdAt = q.value(10).toLongLong();
    a.updatedAt = q.value(11).toLongLong();
    return a;
}

Result<QList<Album>> readAll(Result<QSqlQuery> r)
{
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<Album> out;
    while (q.next())
        out.append(readAlbum(q));
    return out;
}

Result<std::optional<Album>> readOne(Result<QSqlQuery> r)
{
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    if (!q.next())
        return std::optional<Album>();
    return std::optional<Album>(readAlbum(q));
}

QVariant nullIfZero(int v)
{
    return v == 0 ? QVariant() : QVariant(v);
}

QVariant nullIfEmpty(const QString& s)
{
    return s.isEmpty() ? QVariant() : QVariant(s);
}

}  // namespace

Result<AlbumId> AlbumRepo::insert(const Album& album)
{
    const QString title = album.isUnsorted ? QStringLiteral("") : Normalizer::display(album.title);  // NOT NULL: empty, not null

    const QString key = album.isUnsorted ? QStringLiteral("") : Normalizer::identityKey(title);
    auto r = m_db.exec(
        QStringLiteral("INSERT INTO album(primary_artist_id, title, title_key, dir_name, type, is_unsorted, year, "
                       "month, day, cover_path, created_at, updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"),
        {album.primaryArtist.value, title, key, album.dirName, toDbString(album.type), album.isUnsorted ? 1 : 0,
         album.date.year, nullIfZero(album.date.month), nullIfZero(album.date.day), nullIfEmpty(album.coverPath),
         album.createdAt, album.updatedAt});
    if (!r.ok())
        return r.error();
    return AlbumId(m_db.lastInsertId());
}

Result<std::optional<Album>> AlbumRepo::get(AlbumId id)
{
    return readOne(m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM album WHERE id = ?"),
                            {id.value}));
}

Result<std::optional<Album>> AlbumRepo::findByTitle(ArtistId primary, const QString& title)
{
    return readOne(m_db.run(QStringLiteral("SELECT ") + kColumns +
                                QStringLiteral(" FROM album WHERE primary_artist_id = ? AND is_unsorted = 0 AND "
                                               "title_key = ?"),
                            {primary.value, Normalizer::identityKey(title)}));
}

Result<std::optional<Album>> AlbumRepo::unsortedOf(ArtistId primary)
{
    return readOne(m_db.run(QStringLiteral("SELECT ") + kColumns +
                                QStringLiteral(" FROM album WHERE primary_artist_id = ? AND is_unsorted = 1"),
                            {primary.value}));
}

Result<QList<Album>> AlbumRepo::byPrimary(ArtistId primary)
{
    return readAll(m_db.run(QStringLiteral("SELECT ") + kColumns +
                                QStringLiteral(" FROM album WHERE primary_artist_id = ? ORDER BY id"),
                            {primary.value}));
}

Result<QList<Album>> AlbumRepo::byCoAuthor(ArtistId coAuthor)
{
    return readAll(m_db.run(QStringLiteral("SELECT ") + kColumns +
                                QStringLiteral(" FROM album WHERE id IN (SELECT album_id FROM album_artist WHERE "
                                               "artist_id = ?) ORDER BY id"),
                            {coAuthor.value}));
}

Result<QList<Album>> AlbumRepo::all()
{
    return readAll(m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM album ORDER BY id")));
}

Result<void> AlbumRepo::setOwnership(AlbumId id, ArtistId primary, const QString& title, const QString& dirName,
                                     qint64 now)
{
    const QString display = Normalizer::display(title);
    return m_db.exec(QStringLiteral("UPDATE album SET primary_artist_id = ?, title = ?, title_key = ?, dir_name = ?, "
                                    "updated_at = ? WHERE id = ?"),
                     {primary.value, display, display.isEmpty() ? QStringLiteral("") : Normalizer::identityKey(display),
                      dirName, now, id.value});
}

Result<void> AlbumRepo::setCover(AlbumId id, const QString& coverPath, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE album SET cover_path = ?, updated_at = ? WHERE id = ?"),
                     {nullIfEmpty(coverPath), now, id.value});
}

Result<void> AlbumRepo::setMeta(AlbumId id, AlbumType type, const ReleaseDate& date, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE album SET type = ?, year = ?, month = ?, day = ?, updated_at = ? WHERE id = ?"),
                     {toDbString(type), date.year, nullIfZero(date.month), nullIfZero(date.day), now, id.value});
}

Result<void> AlbumRepo::reorder(AlbumId id, const QList<TrackId>& order)
{
    int position = 1;
    for (TrackId t : order) {
        if (auto r = m_db.exec(QStringLiteral("UPDATE album_track SET position = ? WHERE album_id = ? AND track_id = ?"),
                               {position++, id.value, t.value});
            !r.ok())
            return r.error();
    }
    return {};
}

Result<void> AlbumRepo::remove(AlbumId id)
{
    return m_db.exec(QStringLiteral("DELETE FROM album WHERE id = ?"), {id.value});
}

Result<bool> AlbumRepo::dirNameTaken(ArtistId primary, const QString& dirName)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM album WHERE primary_artist_id = ? AND dir_name = ? "
                                        "COLLATE NOCASE"),
                         {primary.value, dirName});
    if (!v.ok())
        return v.error();
    return v.value().toInt() > 0;
}

Result<QList<CoAuthor>> AlbumRepo::coAuthors(AlbumId id)
{
    auto r = m_db.run(QStringLiteral("SELECT artist_id, position FROM album_artist WHERE album_id = ? ORDER BY "
                                     "position"),
                      {id.value});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<CoAuthor> out;
    while (q.next())
        out.append({ArtistId(q.value(0).toLongLong()), q.value(1).toInt()});
    return out;
}

Result<void> AlbumRepo::setCoAuthors(AlbumId id, const QList<CoAuthor>& coAuthors)
{
    if (auto r = m_db.exec(QStringLiteral("DELETE FROM album_artist WHERE album_id = ?"), {id.value}); !r.ok())
        return r.error();
    int position = 1;
    for (const CoAuthor& c : coAuthors) {
        if (auto r = m_db.exec(QStringLiteral("INSERT INTO album_artist(album_id, artist_id, position) VALUES(?,?,?)"),
                               {id.value, c.artist.value, position++});
            !r.ok())
            return r.error();
    }
    return {};
}

Result<int> AlbumRepo::coAuthorCount(ArtistId artist)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM album_artist WHERE artist_id = ?"), {artist.value});
    if (!v.ok())
        return v.error();
    return v.value().toInt();
}

Result<QList<TrackId>> AlbumRepo::tracks(AlbumId id)
{
    auto r = m_db.run(QStringLiteral("SELECT track_id FROM album_track WHERE album_id = ? ORDER BY position"),
                      {id.value});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<TrackId> out;
    while (q.next())
        out.append(TrackId(q.value(0).toLongLong()));
    return out;
}

Result<int> AlbumRepo::trackCount(AlbumId id)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM album_track WHERE album_id = ?"), {id.value});
    if (!v.ok())
        return v.error();
    return v.value().toInt();
}

Result<void> AlbumRepo::addTrack(AlbumId id, TrackId track, int position, qint64 addedAt)
{
    if (auto r = m_db.exec(QStringLiteral("UPDATE album_track SET position = position + 1 WHERE album_id = ? AND "
                                          "position >= ?"),
                           {id.value, position});
        !r.ok())
        return r.error();
    return m_db.exec(QStringLiteral("INSERT INTO album_track(album_id, track_id, position, added_at) VALUES(?,?,?,?)"),
                     {id.value, track.value, position, addedAt});
}

Result<void> AlbumRepo::removeTrack(AlbumId id, TrackId track)
{
    return m_db.exec(QStringLiteral("DELETE FROM album_track WHERE album_id = ? AND track_id = ?"),
                     {id.value, track.value});
}

Result<void> AlbumRepo::renumber(AlbumId id)
{
    // Row by row on purpose: a single UPDATE with a correlated sub-select would see its own earlier
    // changes and produce wrong ranks.
    auto r = m_db.run(QStringLiteral("SELECT track_id, position FROM album_track WHERE album_id = ? ORDER BY position, "
                                     "track_id"),
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
            if (auto u = m_db.exec(QStringLiteral("UPDATE album_track SET position = ? WHERE album_id = ? AND "
                                                  "track_id = ?"),
                                   {expected, id.value, row.first});
                !u.ok())
                return u.error();
        }
        ++expected;
    }
    return {};
}

Result<int> AlbumRepo::positionOf(AlbumId id, TrackId track)
{
    auto v = m_db.scalar(QStringLiteral("SELECT position FROM album_track WHERE album_id = ? AND track_id = ?"),
                         {id.value, track.value});
    if (!v.ok())
        return v.error();
    return v.value().isValid() ? v.value().toInt() : 0;
}

}  // namespace semplice
