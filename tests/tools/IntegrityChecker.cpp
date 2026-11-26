#include "IntegrityChecker.h"

#include <QSqlRecord>

namespace semplice::testing {

namespace {

// Runs a query that selects offending rows; every row becomes one problem line "<what>: <col0> <col1>...".
void expectNoRows(Database& db, QStringList& problems, const QString& what, const QString& sql)
{
    auto r = db.run(sql);
    if (!r.ok()) {
        problems << what + QStringLiteral(": query failed: ") + r.error().devMessage;
        return;
    }
    QSqlQuery q = std::move(r).value();
    int shown = 0;
    while (q.next()) {
        if (++shown > 5) {
            problems << what + QStringLiteral(": ...more");
            break;
        }
        QStringList cols;
        for (int i = 0; i < q.record().count(); ++i)
            cols << q.value(i).toString();
        problems << what + QStringLiteral(": ") + cols.join(QLatin1Char(' '));
    }
}

}  // namespace

QStringList checkLibraryIntegrity(Database& db)
{
    QStringList p;

    expectNoRows(db, p, QStringLiteral("album without tracks"),
                 QStringLiteral("SELECT id FROM album WHERE id NOT IN (SELECT album_id FROM album_track)"));
    expectNoRows(db, p, QStringLiteral("playlist without tracks"),
                 QStringLiteral("SELECT id FROM playlist WHERE id NOT IN (SELECT playlist_id FROM playlist_track)"));
    expectNoRows(db, p, QStringLiteral("track in no container"),
                 QStringLiteral("SELECT id FROM track WHERE id NOT IN (SELECT track_id FROM album_track) AND id NOT IN "
                                "(SELECT track_id FROM playlist_track)"));
    expectNoRows(db, p, QStringLiteral("track without credit"),
                 QStringLiteral("SELECT id FROM track WHERE id NOT IN (SELECT track_id FROM track_artist)"));

    // positions are 1..n
    expectNoRows(db, p, QStringLiteral("album positions not contiguous"),
                 QStringLiteral("SELECT album_id FROM album_track GROUP BY album_id HAVING MIN(position) <> 1 OR "
                                "MAX(position) <> COUNT(*) OR COUNT(DISTINCT position) <> COUNT(*)"));
    expectNoRows(db, p, QStringLiteral("playlist positions not contiguous"),
                 QStringLiteral("SELECT playlist_id FROM playlist_track GROUP BY playlist_id HAVING MIN(position) <> 1 "
                                "OR MAX(position) <> COUNT(*) OR COUNT(DISTINCT position) <> COUNT(*)"));
    expectNoRows(db, p, QStringLiteral("co-author positions not contiguous"),
                 QStringLiteral("SELECT album_id FROM album_artist GROUP BY album_id HAVING MIN(position) <> 1 OR "
                                "MAX(position) <> COUNT(*)"));
    expectNoRows(db, p, QStringLiteral("co-author is also primary"),
                 QStringLiteral("SELECT a.id FROM album a JOIN album_artist aa ON aa.album_id = a.id AND aa.artist_id "
                                "= a.primary_artist_id"));

    // home location
    expectNoRows(db, p, QStringLiteral("home album missing or track not in it"),
                 QStringLiteral("SELECT t.id FROM track t WHERE t.home_kind = 'album' AND NOT EXISTS (SELECT 1 FROM "
                                "album_track x WHERE x.album_id = t.home_id AND x.track_id = t.id)"));
    expectNoRows(db, p, QStringLiteral("home playlist missing or track not in it"),
                 QStringLiteral("SELECT t.id FROM track t WHERE t.home_kind = 'playlist' AND NOT EXISTS (SELECT 1 "
                                "FROM playlist_track x WHERE x.playlist_id = t.home_id AND x.track_id = t.id)"));
    expectNoRows(db, p, QStringLiteral("album track file outside its home folder"),
                 QStringLiteral("SELECT t.id, t.file_path FROM track t JOIN album a ON a.id = t.home_id JOIN artist r "
                                "ON r.id = a.primary_artist_id WHERE t.home_kind = 'album' AND t.file_path NOT LIKE "
                                "'Artists/' || r.dir_name || '/' || a.dir_name || '/%'"));
    expectNoRows(db, p, QStringLiteral("playlist track file outside its home folder"),
                 QStringLiteral("SELECT t.id, t.file_path FROM track t JOIN playlist l ON l.id = t.home_id WHERE "
                                "t.home_kind = 'playlist' AND t.file_path NOT LIKE 'Playlists/' || l.dir_name || "
                                "'/%'"));
    expectNoRows(db, p, QStringLiteral("temporary path left behind"),
                 QStringLiteral("SELECT id, file_path FROM track WHERE file_path LIKE char(1) || '%'"));

    // artists
    expectNoRows(db, p, QStringLiteral("hidden artist has a folder"),
                 QStringLiteral("SELECT id FROM artist WHERE hidden = 1 AND dir_name IS NOT NULL"));
    expectNoRows(db, p, QStringLiteral("hidden artist owns an album"),
                 QStringLiteral("SELECT r.id FROM artist r JOIN album a ON a.primary_artist_id = r.id WHERE r.hidden "
                                "= 1"));
    expectNoRows(db, p, QStringLiteral("hidden artist nobody references"),
                 QStringLiteral("SELECT id FROM artist WHERE hidden = 1 AND id NOT IN (SELECT artist_id FROM "
                                "track_artist) AND id NOT IN (SELECT artist_id FROM album_artist)"));
    expectNoRows(db, p, QStringLiteral("unsorted album has a title or wrong type"),
                 QStringLiteral("SELECT id FROM album WHERE is_unsorted = 1 AND (title <> '' OR type <> 'other' OR "
                                "dir_name <> 'Unsorted tracks')"));

    // recent list
    expectNoRows(db, p, QStringLiteral("recent item points nowhere"),
                 QStringLiteral("SELECT kind, item_id FROM recent_item WHERE (kind = 'album' AND item_id NOT IN "
                                "(SELECT id FROM album)) OR (kind = 'playlist' AND item_id NOT IN (SELECT id FROM "
                                "playlist))"));

    // foreign keys
    expectNoRows(db, p, QStringLiteral("foreign key violation"), QStringLiteral("PRAGMA foreign_key_check"));
    return p;
}

}  // namespace semplice::testing
