#include "core/recommend/LibraryRecommendationSource.h"

#include <QHash>
#include <QSqlQuery>
#include <QThreadPool>

#include <memory>

#include <algorithm>

namespace semplice {

Result<RecommendationPool> loadRecommendationPool(Database& db, const GenreVocabulary& genres)
{
    RecommendationPool pool;
    const QStringList keys = genres.allKeys();   // genres, and the language and the mood of the music
    QHash<QString, int> tagIndex;
    for (int i = 0; i < keys.size(); ++i)
        tagIndex.insert(keys.at(i), i);
    pool.relations.related.resize(keys.size());
    pool.relations.opposite.resize(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
        for (const QString& k : genres.related(keys.at(i)))
            if (tagIndex.contains(k))
                pool.relations.related[i].append(tagIndex.value(k));
        for (const QString& k : genres.opposite(keys.at(i)))
            if (tagIndex.contains(k))
                pool.relations.opposite[i].append(tagIndex.value(k));
    }

    QHash<qint64, int> at;  // track id -> position
    {
        auto rows = db.run(QStringLiteral("SELECT id, COALESCE(last_played_at, 0) FROM track ORDER BY id"));
        if (!rows.ok())
            return rows.error();
        QSqlQuery q = std::move(rows).value();
        while (q.next()) {
            TrackFeatures f;
            f.id = TrackId(q.value(0).toLongLong());
            f.lastPlayedMs = q.value(1).toLongLong();
            at.insert(f.id.value, static_cast<int>(pool.tracks.size()));
            pool.tracks.append(f);
        }
    }
    auto each = [&](const QString& sql, auto&& fn) -> Result<void> {
        auto rows = db.run(sql);
        if (!rows.ok())
            return rows.error();
        QSqlQuery q = std::move(rows).value();
        while (q.next()) {
            auto it = at.constFind(q.value(0).toLongLong());
            if (it != at.constEnd())
                fn(pool.tracks[it.value()], q);
        }
        return {};
    };
    auto r = each(QStringLiteral("SELECT at.track_id, a.id, a.year FROM album_track at JOIN album a ON a.id = at.album_id ORDER BY a.id"),
                  [](TrackFeatures& f, QSqlQuery& q) {
                      f.albums.append(q.value(1).toLongLong());
                      if (f.album == 0)
                          f.album = q.value(1).toLongLong();
                      const int year = q.value(2).toInt();
                      if (year > 0 && (f.year == 0 || year < f.year))
                          f.year = year;
                  });
    if (!r.ok())
        return r.error();
    r = each(QStringLiteral("SELECT track_id, artist_id FROM track_artist ORDER BY track_id, position"),
             [](TrackFeatures& f, QSqlQuery& q) { f.artists.append(q.value(1).toLongLong()); });
    if (!r.ok())
        return r.error();
    auto addTag = [&](TrackFeatures& f, QSqlQuery& q) {
        auto it = tagIndex.constFind(q.value(1).toString());
        if (it != tagIndex.constEnd() && !f.tags.contains(it.value()))
            f.tags.append(it.value());
    };
    r = each(QStringLiteral("SELECT track_id, tag_key FROM track_tag"), addTag);
    if (!r.ok())
        return r.error();
    r = each(QStringLiteral("SELECT at.track_id, t.tag_key FROM album_track at JOIN album_tag t ON t.album_id = at.album_id"), addTag);
    if (!r.ok())
        return r.error();
    for (TrackFeatures& f : pool.tracks)
        std::sort(f.tags.begin(), f.tags.end());
    return pool;
}

LibraryRecommendationSource::LibraryRecommendationSource(QString dbPath, GenreVocabulary genres, const IClock& clock, quint64 seed, RecommenderConfig config)
    : m_dbPath(std::move(dbPath)), m_genres(std::move(genres)), m_clock(clock), m_random(seed), m_selector(config, m_random)
{
    refresh();
}

LibraryRecommendationSource::~LibraryRecommendationSource()
{
    if (m_building && m_build.valid())
        m_build.wait();
}

void LibraryRecommendationSource::refresh()
{
    // never wait here (the caller is the GUI thread): one build at a time, a change that arrives meanwhile starts the next
    if (m_building) {
        if (m_build.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            m_dirty = true;
            return;
        }
        collect();
    }
    m_dirty = false;
    m_building = true;
    const QString path = m_dbPath;
    const GenreVocabulary genres = m_genres;
    // On Qt's thread pool, not std::async: a thread that is made and ended for every refresh (the MinGW runtime's own thread
    // start and exit) went together with a one-second stand-still of the whole program after every save.
    auto promise = std::make_shared<std::promise<RecommendationPool>>();
    m_build = promise->get_future();
    QThreadPool::globalInstance()->start([path, genres, promise] {
        // a connection of its own: it belongs to this worker thread
        auto db = Database::open(path, true);
        if (!db.ok()) {
            promise->set_value({});
            return;
        }
        auto pool = loadRecommendationPool(*db.value(), genres);
        promise->set_value(pool.ok() ? std::move(pool).value() : RecommendationPool{});
    });
}

void LibraryRecommendationSource::collect()
{
    if (!m_building || !m_build.valid())
        return;
    RecommendationPool pool = m_build.get();
    m_building = false;
    m_selector.setPool(std::move(pool.tracks), std::move(pool.relations));
    if (m_dirty)
        refresh();  // the library changed while that pool was being read
}

QList<TrackId> LibraryRecommendationSource::pick(const RecommendationRequest& request)
{
    if (m_building) {
        if (m_build.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            collect();
        else if (m_selector.poolSize() == 0)
            collect();  // nothing to work with yet: wait for the first build
    }
    PickRequest r;
    r.seeds = request.seedTracks;
    r.exclude = request.exclude;
    r.count = request.count;
    r.nowMs = m_clock.now().toMSecsSinceEpoch();
    return m_selector.pick(r);
}

}  // namespace semplice
