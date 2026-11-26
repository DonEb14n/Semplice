#include "core/browse/LibraryQueries.h"

namespace semplice {

namespace {

QString direction(const LibrarySort& sort)
{
    return sort.direction == SortDirection::Ascending ? QStringLiteral("ASC") : QStringLiteral("DESC");
}

}  // namespace

bool LibraryQueries::isAllowed(LibraryTab tab, SortKey key)
{
    return key != SortKey::Year || tab == LibraryTab::Albums;
}

Result<QList<ArtistRow>> LibraryQueries::artists(const LibrarySort& sort, bool onlyHidden)
{
    const QString order = sort.key == SortKey::Alphabet ? QStringLiteral("name_key %1, id").arg(direction(sort))
                                                         : QStringLiteral("updated_at %1, id %1").arg(direction(sort));
    auto rows = m_db.run(QStringLiteral("SELECT id, name, COALESCE(avatar_path, ''), user_hidden FROM artist WHERE hidden = 0 AND user_hidden = ") + (onlyHidden ? QStringLiteral("1") : QStringLiteral("0")) + QStringLiteral(" ORDER BY ") + order);
    if (!rows.ok())
        return rows.error();
    QSqlQuery q = std::move(rows).value();
    QList<ArtistRow> out;
    while (q.next())
        out.append({ArtistId(q.value(0).toLongLong()), q.value(1).toString(), q.value(2).toString(), q.value(3).toInt() != 0});
    return out;
}

Result<QList<AlbumRow>> LibraryQueries::albums(const LibrarySort& sort, const QSet<AlbumType>& types)
{
    QString where = QStringLiteral("a.is_unsorted = 0");
    if (!types.isEmpty()) {
        QStringList quoted;
        for (AlbumType t : types)
            quoted << QStringLiteral("'%1'").arg(toDbString(t));
        quoted.sort();
        where += QStringLiteral(" AND a.type IN (%1)").arg(quoted.join(QLatin1Char(',')));
    }
    QString order;
    switch (sort.key) {
    case SortKey::Alphabet: order = QStringLiteral("a.title_key %1, a.id").arg(direction(sort)); break;
    case SortKey::Year:
        order = QStringLiteral("a.year %1, COALESCE(a.month, 0) %1, COALESCE(a.day, 0) %1, a.id").arg(direction(sort));
        break;
    case SortKey::Recent: order = QStringLiteral("a.updated_at %1, a.id %1").arg(direction(sort)); break;
    }
    const QString sql = QStringLiteral(
                            "SELECT a.id, a.title, a.primary_artist_id, a.type, a.year, COALESCE(a.cover_path, ''), "
                            "(SELECT name FROM artist WHERE id = a.primary_artist_id), "
                            "(SELECT group_concat(n, ', ') FROM (SELECT ar.name AS n FROM album_artist aa "
                            "  JOIN artist ar ON ar.id = aa.artist_id WHERE aa.album_id = a.id ORDER BY aa.position)) "
                            "FROM album a WHERE %1 ORDER BY %2")
                            .arg(where, order);
    auto rows = m_db.run(sql);
    if (!rows.ok())
        return rows.error();
    QSqlQuery q = std::move(rows).value();
    QList<AlbumRow> out;
    while (q.next()) {
        AlbumRow r;
        r.id = AlbumId(q.value(0).toLongLong());
        r.title = q.value(1).toString();
        r.primaryArtist = ArtistId(q.value(2).toLongLong());
        r.type = albumTypeFromString(q.value(3).toString()).value_or(AlbumType::Other);
        r.year = q.value(4).toInt();
        r.coverPath = q.value(5).toString();
        r.artists = q.value(6).toString();
        const QString co = q.value(7).toString();
        if (!co.isEmpty())
            r.artists += QStringLiteral(", ") + co;
        out.append(r);
    }
    return out;
}

Result<QList<PlaylistRow>> LibraryQueries::playlists(const LibrarySort& sort)
{
    const QString order = sort.key == SortKey::Alphabet ? QStringLiteral("name_key %1, id").arg(direction(sort))
                                                         : QStringLiteral("updated_at %1, id %1").arg(direction(sort));
    auto rows = m_db.run(QStringLiteral("SELECT id, name, COALESCE(cover_path, '') FROM playlist ORDER BY ") + order);
    if (!rows.ok())
        return rows.error();
    QSqlQuery q = std::move(rows).value();
    QList<PlaylistRow> out;
    while (q.next())
        out.append({PlaylistId(q.value(0).toLongLong()), q.value(1).toString(), q.value(2).toString()});
    return out;
}

}  // namespace semplice
