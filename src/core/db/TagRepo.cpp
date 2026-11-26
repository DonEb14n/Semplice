#include "core/db/TagRepo.h"

namespace semplice {

namespace {

struct TagTable {
    const char* table;
    const char* column;
};

TagTable tableFor(TagOwner owner)
{
    switch (owner) {
    case TagOwner::Artist: return {"artist_tag", "artist_id"};
    case TagOwner::Album: return {"album_tag", "album_id"};
    case TagOwner::Track: return {"track_tag", "track_id"};
    }
    return {"track_tag", "track_id"};
}

}  // namespace

Result<QStringList> TagRepo::tags(TagOwner owner, qint64 id)
{
    const TagTable t = tableFor(owner);
    auto r = m_db.run(QStringLiteral("SELECT tag_key FROM %1 WHERE %2 = ? ORDER BY tag_key")
                          .arg(QLatin1String(t.table), QLatin1String(t.column)),
                      {id});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QStringList out;
    while (q.next())
        out << q.value(0).toString();
    return out;
}

Result<void> TagRepo::setTags(TagOwner owner, qint64 id, const QStringList& keys)
{
    const TagTable t = tableFor(owner);
    if (auto r = m_db.exec(QStringLiteral("DELETE FROM %1 WHERE %2 = ?")
                               .arg(QLatin1String(t.table), QLatin1String(t.column)),
                           {id});
        !r.ok())
        return r.error();
    return addTags(owner, id, keys);
}

Result<void> TagRepo::addTags(TagOwner owner, qint64 id, const QStringList& keys)
{
    const TagTable t = tableFor(owner);
    for (const QString& key : keys) {
        if (auto r = m_db.exec(QStringLiteral("INSERT OR IGNORE INTO %1(%2, tag_key) VALUES(?,?)")
                                   .arg(QLatin1String(t.table), QLatin1String(t.column)),
                               {id, key});
            !r.ok())
            return r.error();
    }
    return {};
}

Result<void> RecentRepo::touch(ContainerKind kind, qint64 id, qint64 playedAt)
{
    if (auto r = m_db.exec(QStringLiteral("INSERT INTO recent_item(kind, item_id, played_at) VALUES(?,?,?) ON "
                                          "CONFLICT(kind, item_id) DO UPDATE SET played_at = excluded.played_at"),
                           {toDbString(kind), id, playedAt});
        !r.ok())
        return r.error();
    return m_db.exec(QStringLiteral("DELETE FROM recent_item WHERE rowid NOT IN (SELECT rowid FROM recent_item ORDER "
                                    "BY played_at DESC, rowid DESC LIMIT %1)")
                         .arg(kMaxItems));
}

Result<void> RecentRepo::remove(ContainerKind kind, qint64 id)
{
    return m_db.exec(QStringLiteral("DELETE FROM recent_item WHERE kind = ? AND item_id = ?"), {toDbString(kind), id});
}

Result<QList<Membership>> RecentRepo::list()
{
    auto r = m_db.run(QStringLiteral("SELECT kind, item_id, played_at FROM recent_item ORDER BY played_at DESC, rowid "
                                     "DESC"));
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<Membership> out;
    int rank = 1;
    while (q.next())
        out.append({q.value(0).toString() == QLatin1String("album") ? ContainerKind::Album : ContainerKind::Playlist,
                    q.value(1).toLongLong(), rank++, q.value(2).toLongLong()});
    return out;
}

Result<std::optional<QString>> KvRepo::get(const QString& key)
{
    auto r = m_db.run(QStringLiteral("SELECT value FROM kv WHERE key = ?"), {key});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    if (!q.next())
        return std::optional<QString>();
    return std::optional<QString>(q.value(0).toString());
}

Result<void> KvRepo::set(const QString& key, const QString& value)
{
    return m_db.exec(QStringLiteral("INSERT INTO kv(key, value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value = "
                                    "excluded.value"),
                     {key, value});
}

Result<void> KvRepo::remove(const QString& key)
{
    return m_db.exec(QStringLiteral("DELETE FROM kv WHERE key = ?"), {key});
}

}  // namespace semplice
