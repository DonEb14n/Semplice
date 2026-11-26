#include "core/db/ArtistRepo.h"

#include "core/util/Normalizer.h"

namespace semplice {

namespace {

const QString kColumns = QStringLiteral(
    "id, name, dir_name, hidden, avatar_path, tags_seeded, created_at, updated_at, user_hidden");

Artist readArtist(QSqlQuery& q)
{
    Artist a;
    a.id = ArtistId(q.value(0).toLongLong());
    a.name = q.value(1).toString();
    a.dirName = q.value(2).toString();
    a.hidden = q.value(3).toInt() != 0;
    a.avatarPath = q.value(4).toString();
    a.tagsSeeded = q.value(5).toInt() != 0;
    a.createdAt = q.value(6).toLongLong();
    a.updatedAt = q.value(7).toLongLong();
    a.userHidden = q.value(8).toInt() != 0;
    return a;
}

Result<QList<Artist>> readAll(Result<QSqlQuery> r)
{
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    QList<Artist> out;
    while (q.next())
        out.append(readArtist(q));
    return out;
}

}  // namespace

Result<ArtistId> ArtistRepo::insert(const Artist& artist)
{
    const QString name = Normalizer::display(artist.name);
    auto r = m_db.exec(
        QStringLiteral("INSERT INTO artist(name, name_key, dir_name, hidden, avatar_path, tags_seeded, "
                       "created_at, updated_at) VALUES(?,?,?,?,?,?,?,?)"),
        {name, Normalizer::identityKey(name), artist.dirName.isEmpty() ? QVariant() : QVariant(artist.dirName),
         artist.hidden ? 1 : 0, artist.avatarPath.isEmpty() ? QVariant() : QVariant(artist.avatarPath),
         artist.tagsSeeded ? 1 : 0, artist.createdAt, artist.updatedAt});
    if (!r.ok())
        return r.error();
    return ArtistId(m_db.lastInsertId());
}

Result<std::optional<Artist>> ArtistRepo::get(ArtistId id)
{
    auto r = m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM artist WHERE id = ?"),
                      {id.value});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    if (!q.next())
        return std::optional<Artist>();
    return std::optional<Artist>(readArtist(q));
}

Result<std::optional<Artist>> ArtistRepo::findByName(const QString& name)
{
    auto r = m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM artist WHERE name_key = ?"),
                      {Normalizer::identityKey(name)});
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    if (!q.next())
        return std::optional<Artist>();
    return std::optional<Artist>(readArtist(q));
}

Result<QList<Artist>> ArtistRepo::all()
{
    return readAll(m_db.run(QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM artist ORDER BY id")));
}

Result<QList<Artist>> ArtistRepo::visible()
{
    return readAll(m_db.run(QStringLiteral("SELECT ") + kColumns +
                            QStringLiteral(" FROM artist WHERE hidden = 0 ORDER BY id")));
}

Result<void> ArtistRepo::setUserHidden(ArtistId id, bool userHidden, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE artist SET user_hidden = ?, updated_at = ? WHERE id = ?"),
                     {userHidden ? 1 : 0, now, id.value});
}

Result<void> ArtistRepo::setVisibility(ArtistId id, bool hidden, const QString& dirName, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE artist SET hidden = ?, dir_name = ?, updated_at = ? WHERE id = ?"),
                     {hidden ? 1 : 0, dirName.isEmpty() ? QVariant() : QVariant(dirName), now, id.value});
}

Result<void> ArtistRepo::setName(ArtistId id, const QString& name, const QString& dirName, qint64 now)
{
    const QString display = Normalizer::display(name);
    return m_db.exec(QStringLiteral("UPDATE artist SET name = ?, name_key = ?, dir_name = ?, updated_at = ? WHERE id = ?"),
                     {display, Normalizer::identityKey(display), dirName.isEmpty() ? QVariant() : QVariant(dirName), now, id.value});
}

Result<void> ArtistRepo::setTagsSeeded(ArtistId id, bool seeded)
{
    return m_db.exec(QStringLiteral("UPDATE artist SET tags_seeded = ? WHERE id = ?"), {seeded ? 1 : 0, id.value});
}

Result<void> ArtistRepo::setAvatar(ArtistId id, const QString& avatarPath, qint64 now)
{
    return m_db.exec(QStringLiteral("UPDATE artist SET avatar_path = ?, updated_at = ? WHERE id = ?"),
                     {avatarPath.isEmpty() ? QVariant() : QVariant(avatarPath), now, id.value});
}

Result<void> ArtistRepo::remove(ArtistId id)
{
    return m_db.exec(QStringLiteral("DELETE FROM artist WHERE id = ?"), {id.value});
}

Result<bool> ArtistRepo::dirNameTaken(const QString& dirName)
{
    auto v = m_db.scalar(QStringLiteral("SELECT COUNT(*) FROM artist WHERE dir_name = ? COLLATE NOCASE"), {dirName});
    if (!v.ok())
        return v.error();
    return v.value().toInt() > 0;
}

}  // namespace semplice
