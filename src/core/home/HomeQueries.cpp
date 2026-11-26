#include "core/home/HomeQueries.h"

#include <algorithm>

namespace semplice {

namespace {

struct Where {
    QString clause;
    QVariantList params;
    QString order;
};

Where whereFor(const CategorySpec& spec)
{
    const QString base = QStringLiteral("a.is_unsorted = 0");
    const QString recent = QStringLiteral("a.updated_at DESC, a.id DESC");
    switch (spec.type) {
    case CategoryType::ArtistDiscography:
        return {base + QStringLiteral(" AND (a.primary_artist_id = ? OR a.id IN (SELECT album_id FROM album_artist WHERE artist_id = ?))"),
                {spec.arg.toLongLong(), spec.arg.toLongLong()},
                QStringLiteral("a.year DESC, COALESCE(a.month, 0) DESC, COALESCE(a.day, 0) DESC, a.created_at DESC, a.id DESC")};
    case CategoryType::Year: return {base + QStringLiteral(" AND a.year = ?"), {spec.arg.toInt()}, recent};
    case CategoryType::Decade:
        return {base + QStringLiteral(" AND a.year >= ? AND a.year < ?"), {spec.arg.toInt(), spec.arg.toInt() + 10}, recent};
    case CategoryType::Genre: {  // "rap", or tags joined with "+": "mood:cheerful+lang:en+rap" (every one of them)
        QString where = base;
        QVariantList args;
        for (const QString& key : spec.arg.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
            where += QStringLiteral(" AND a.id IN (SELECT album_id FROM album_tag WHERE tag_key = ?)");
            args << key;
        }
        return {where, args, recent};
    }
    case CategoryType::YourAlbums: return {base, {}, recent};
    case CategoryType::YourPlaylists: break;
    }
    return {base, {}, recent};
}

}  // namespace

Result<bool> HomeQueries::hasContent()
{
    auto n = m_db.scalar(QStringLiteral("SELECT (SELECT COUNT(*) FROM album WHERE is_unsorted = 0) + (SELECT COUNT(*) FROM playlist)"));
    if (!n.ok())
        return n.error();
    return n.value().toLongLong() > 0;
}

QList<ICategorySource::Instance> HomeQueries::instances(CategoryType type)
{
    QList<Instance> out;
    auto collect = [&](const QString& sql, const QVariantList& args, auto&& make) {
        auto rows = m_db.run(sql, args);
        if (!rows.ok())
            return;
        QSqlQuery q = std::move(rows).value();
        while (q.next())
            out.append(make(q));
    };
    switch (type) {
    case CategoryType::ArtistDiscography:
        collect(QStringLiteral(
                    "SELECT ar.id, ar.name FROM artist ar WHERE ar.hidden = 0 AND ar.user_hidden = 0 AND "
                    "(SELECT COUNT(*) FROM album a WHERE a.is_unsorted = 0 AND (a.primary_artist_id = ar.id OR "
                    " a.id IN (SELECT album_id FROM album_artist WHERE artist_id = ar.id))) >= ?"),
                {CategoryGenerator::kMinArtistAlbums},
                [](QSqlQuery& q) { return Instance{QString::number(q.value(0).toLongLong()), q.value(1).toString()}; });
        break;
    case CategoryType::Year:
        collect(QStringLiteral("SELECT year FROM album WHERE is_unsorted = 0 GROUP BY year HAVING COUNT(*) >= ?"),
                {CategoryGenerator::kMinAlbums}, [](QSqlQuery& q) { return Instance{QString::number(q.value(0).toInt()), {}}; });
        break;
    case CategoryType::Decade:
        collect(QStringLiteral("SELECT (year / 10) * 10 FROM album WHERE is_unsorted = 0 GROUP BY year / 10 HAVING COUNT(*) >= ?"),
                {CategoryGenerator::kMinAlbums}, [](QSqlQuery& q) { return Instance{QString::number(q.value(0).toInt()), {}}; });
        break;
    case CategoryType::Genre:
        collect(QStringLiteral("SELECT t.tag_key FROM album_tag t JOIN album a ON a.id = t.album_id WHERE a.is_unsorted = 0 "
                               "AND t.tag_key NOT LIKE '%:%' GROUP BY t.tag_key HAVING COUNT(*) >= ?"),
                {CategoryGenerator::kMinAlbums}, [](QSqlQuery& q) { return Instance{q.value(0).toString(), {}}; });
        // a genre with the mood, with the language of the lyrics, or with both: "Cheerful English Rap", "Cheerful Rap", "English Rap"
        collect(QStringLiteral("SELECT e.tag_key, g.tag_key FROM album_tag g JOIN album_tag e ON e.album_id = g.album_id JOIN album a ON a.id = g.album_id "
                               "WHERE a.is_unsorted = 0 AND g.tag_key NOT LIKE '%:%' AND e.tag_key LIKE '%:%' "
                               "GROUP BY e.tag_key, g.tag_key HAVING COUNT(*) >= ?"),
                {CategoryGenerator::kMinAlbums},
                [](QSqlQuery& q) { return Instance{q.value(0).toString() + QLatin1Char('+') + q.value(1).toString(), {}}; });
        collect(QStringLiteral("SELECT m.tag_key, l.tag_key, g.tag_key FROM album_tag g JOIN album_tag m ON m.album_id = g.album_id "
                               "JOIN album_tag l ON l.album_id = g.album_id JOIN album a ON a.id = g.album_id "
                               "WHERE a.is_unsorted = 0 AND g.tag_key NOT LIKE '%:%' AND m.tag_key LIKE 'mood:%' AND l.tag_key LIKE 'lang:%' "
                               "GROUP BY m.tag_key, l.tag_key, g.tag_key HAVING COUNT(*) >= ?"),
                {CategoryGenerator::kMinAlbums},
                [](QSqlQuery& q) { return Instance{q.value(0).toString() + QLatin1Char('+') + q.value(1).toString() + QLatin1Char('+') + q.value(2).toString(), {}}; });
        break;
    case CategoryType::YourPlaylists:
        if (auto n = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM playlist")); n.ok() && n.value().toInt() >= CategoryGenerator::kMinPlaylists)
            out.append({QString(), {}});
        break;
    case CategoryType::YourAlbums:
        if (auto n = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM album WHERE is_unsorted = 0"));
            n.ok() && n.value().toInt() >= CategoryGenerator::kMinAlbums)
            out.append({QString(), {}});
        break;
    }
    return out;
}

Result<QList<CarouselItem>> HomeQueries::albums(const QString& where, const QVariantList& params, const QString& order)
{
    const QString sql = QStringLiteral(
                            "SELECT a.id, a.title, a.primary_artist_id, a.type, a.year, COALESCE(a.cover_path, ''), "
                            "(SELECT name FROM artist WHERE id = a.primary_artist_id), "
                            "(SELECT group_concat(n, ', ') FROM (SELECT ar.name AS n FROM album_artist aa "
                            "  JOIN artist ar ON ar.id = aa.artist_id WHERE aa.album_id = a.id ORDER BY aa.position)), "
                            "(SELECT group_concat(i, ',') FROM (SELECT aa.artist_id AS i FROM album_artist aa WHERE aa.album_id = a.id ORDER BY aa.position)), "
                            "(SELECT group_concat(n, char(31)) FROM (SELECT ar.name AS n FROM album_artist aa "
                            "  JOIN artist ar ON ar.id = aa.artist_id WHERE aa.album_id = a.id ORDER BY aa.position)) "
                            "FROM album a WHERE %1 ORDER BY %2")
                            .arg(where, order);
    auto rows = m_db.run(sql, params);
    if (!rows.ok())
        return rows.error();
    QSqlQuery q = std::move(rows).value();
    QList<CarouselItem> out;
    while (q.next()) {
        CarouselItem item;
        item.kind = ContainerKind::Album;
        item.id = q.value(0).toLongLong();
        item.title = q.value(1).toString();
        item.ownerId = q.value(2).toLongLong();
        item.albumType = static_cast<int>(albumTypeFromString(q.value(3).toString()).value_or(AlbumType::Other));
        item.year = q.value(4).toInt();
        item.cover = q.value(5).toString();
        item.subtitle = q.value(6).toString();
        const QString co = q.value(7).toString();
        if (!co.isEmpty())
            item.subtitle += QStringLiteral(", ") + co;
        item.owners.append({item.ownerId, q.value(6).toString()});
        const QStringList coIds = q.value(8).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        const QStringList coNames = q.value(9).toString().split(QChar(31), Qt::SkipEmptyParts);
        for (int i = 0; i < coIds.size() && i < coNames.size(); ++i)
            if (coIds.at(i).toLongLong() != item.ownerId)
                item.owners.append({coIds.at(i).toLongLong(), coNames.at(i)});
        out.append(item);
    }
    return out;
}

Result<qint64> HomeQueries::albumDuration(const QString& where, const QVariantList& params)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COALESCE(SUM(t.duration_ms), 0) FROM album_track atr JOIN track t ON t.id = atr.track_id "
                                        "WHERE atr.album_id IN (SELECT a.id FROM album a WHERE %1)")
                             .arg(where),
                         params);
    if (!v.ok())
        return v.error();
    return v.value().toLongLong();
}

Result<CategoryContents> HomeQueries::contents(const CategorySpec& spec, int limit, std::mt19937_64* rng)
{
    CategoryContents out;
    out.spec = spec;
    if (spec.type == CategoryType::YourPlaylists) {
        auto rows = m_db.run(QStringLiteral("SELECT id, name, COALESCE(cover_path, '') FROM playlist ORDER BY updated_at DESC, id DESC"));
        if (!rows.ok())
            return rows.error();
        QSqlQuery q = std::move(rows).value();
        while (q.next() && (limit <= 0 || out.items.size() < limit)) {
            CarouselItem item;
            item.kind = ContainerKind::Playlist;
            item.id = q.value(0).toLongLong();
            item.title = q.value(1).toString();
            item.cover = q.value(2).toString();
            out.items.append(item);
        }
        auto d = m_db.scalar(QStringLiteral("SELECT COALESCE(SUM(t.duration_ms), 0) FROM playlist_track pt JOIN track t ON t.id = pt.track_id"));
        if (d.ok())
            out.durationMs = d.value().toLongLong();
        return out;
    }
    const Where w = whereFor(spec);
    auto items = albums(w.clause, w.params, w.order);
    if (!items.ok())
        return items.error();
    out.items = std::move(items).value();
    const bool sample = limit > 0 && spec.type != CategoryType::YourAlbums;
    if (sample && rng)
        std::shuffle(out.items.begin(), out.items.end(), *rng);
    if (limit > 0 && out.items.size() > limit)
        out.items.resize(limit);
    auto d = albumDuration(w.clause, w.params);
    if (d.ok())
        out.durationMs = d.value();
    return out;
}

Result<QList<CarouselItem>> HomeQueries::recent(int limit)
{
    auto rows = m_db.run(QStringLiteral("SELECT kind, item_id FROM recent_item ORDER BY played_at DESC LIMIT ?"), {limit});
    if (!rows.ok())
        return rows.error();
    QList<QPair<QString, qint64>> refs;
    QSqlQuery q = std::move(rows).value();
    while (q.next())
        refs.append({q.value(0).toString(), q.value(1).toLongLong()});
    QList<CarouselItem> out;
    for (const auto& ref : std::as_const(refs)) {
        if (ref.first == QLatin1String("album")) {
            auto one = albums(QStringLiteral("a.id = ?"), {ref.second}, QStringLiteral("a.id"));
            if (one.ok() && !one.value().isEmpty())
                out.append(one.value().first());
        } else {
            auto p = m_db.run(QStringLiteral("SELECT id, name, COALESCE(cover_path, '') FROM playlist WHERE id = ?"), {ref.second});
            if (p.ok()) {
                QSqlQuery pq = std::move(p).value();
                if (pq.next()) {
                    CarouselItem item;
                    item.kind = ContainerKind::Playlist;
                    item.id = pq.value(0).toLongLong();
                    item.title = pq.value(1).toString();
                    item.cover = pq.value(2).toString();
                    out.append(item);
                }
            }
        }
    }
    return out;
}

}  // namespace semplice
