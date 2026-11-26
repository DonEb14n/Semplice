#pragma once

#include "core/domain/Ids.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QVector>

#include <deque>
#include <functional>
#include <random>

namespace semplice {

// Every tunable number of the recommender in one place (SPEC 7.6, DEFAULT values).
struct RecommenderConfig {
    double sharedTags = 0.50;
    double sameArtist = 0.30;
    double relatedGenre = 0.20;
    double yearProximity = 0.10;
    double oppositeGenre = -0.30;
    double recentlyPlayed = -0.40;
    double yearSpan = 20.0;                         // years until the proximity score reaches 0
    qint64 recentDecayMs = 7LL * 24 * 3600 * 1000;  // the penalty fades to 0 over this time
    int topCandidates = 100;
    int varietyWindow = 10;                          // positions looked at ...
    int maxPerAlbumInWindow = 2;                     // ... at most this many tracks of one album
    int minPool = 2;                                 // fewer playable candidates than this: stop
    int maxScored = 20000;                           // a bigger pool is sampled first (keeps one pick within a few ms on 200k tracks)
};

// What the scorer knows about one track.
struct TrackFeatures {
    TrackId id;
    qint64 album = 0;               // first album that contains it, 0 = playlist-only
    QList<qint64> albums;           // every album that contains it (seed albums are matched against all)
    QList<qint64> artists;          // credited artists (main and feat)
    QList<int> tags;                // indexes into the tag table, sorted, unique
    int year = 0;                   // 0 = unknown
    qint64 lastPlayedMs = 0;        // 0 = never
};

// Which tag indexes are related / opposite to which (symmetric).
struct TagRelations {
    QVector<QVector<int>> related;
    QVector<QVector<int>> opposite;
    bool isRelated(int a, int b) const { return a < related.size() && related.at(a).contains(b); }
    bool isOpposite(int a, int b) const { return a < opposite.size() && opposite.at(a).contains(b); }
};

// What the source that just finished "is".
struct SeedProfile {
    QSet<int> tags;
    QSet<qint64> artists;
    QSet<qint64> albums;
    double year = 0.0;              // average of the known years, 0 = none
    bool empty = true;
    static SeedProfile of(const QList<const TrackFeatures*>& seeds);
};

struct ScoreParts {
    double tags = 0, artist = 0, related = 0, year = 0, opposite = 0, recent = 0;
    double total() const { return tags + artist + related + year + opposite + recent; }
};

// PURE: inputs -> score.
ScoreParts scoreTrack(const TrackFeatures& track, const SeedProfile& seed, const TagRelations& relations, qint64 nowMs, const RecommenderConfig& config);

class IRandom {
public:
    virtual ~IRandom() = default;
    virtual double next01() = 0;  // [0, 1)
};

class MersenneRandom final : public IRandom {
public:
    explicit MersenneRandom(quint64 seed) : m_engine(seed) {}
    double next01() override { return std::uniform_real_distribution<double>(0.0, 1.0)(m_engine); }

private:
    std::mt19937_64 m_engine;
};

struct PickRequest {
    QList<TrackId> seeds;
    QList<TrackId> exclude;
    int count = 3;
    qint64 nowMs = 0;
};

// Picks tracks from a pool: score, keep the best, choose by weighted chance under the variety rules.
class Selector {
public:
    Selector(RecommenderConfig config, IRandom& random) : m_config(config), m_random(random) {}

    void setPool(QList<TrackFeatures> pool, TagRelations relations);
    int poolSize() const { return static_cast<int>(m_pool.size()); }
    const RecommenderConfig& config() const { return m_config; }

    QList<TrackId> pick(const PickRequest& request);

    // For the debug view: the best candidates with their components, no state change.
    struct Candidate {
        TrackId id;
        ScoreParts parts;
        qint64 album = 0;
    };
    QList<Candidate> top(const PickRequest& request, int n) const;

private:
    QList<const TrackFeatures*> candidates(const PickRequest& request, const SeedProfile& seed, int relaxation) const;
    bool allowed(const TrackFeatures& t, const QList<qint64>& planned, int level) const;

    RecommenderConfig m_config;
    IRandom& m_random;
    QList<TrackFeatures> m_pool;
    QHash<qint64, int> m_index;  // track id -> position in m_pool
    TagRelations m_relations;
    QSet<qint64> m_used;                // recommended before (until the pool is exhausted)
    std::deque<qint64> m_recentAlbums;  // albums of the last picks (0 = none), newest last
};

}  // namespace semplice
