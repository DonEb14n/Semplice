#include "core/recommend/Recommender.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace semplice {

SeedProfile SeedProfile::of(const QList<const TrackFeatures*>& seeds)
{
    SeedProfile p;
    double yearSum = 0;
    int yearCount = 0;
    for (const TrackFeatures* t : seeds) {
        if (!t)
            continue;
        p.empty = false;
        for (int tag : t->tags)
            p.tags.insert(tag);
        for (qint64 a : t->artists)
            p.artists.insert(a);
        for (qint64 al : t->albums)
            p.albums.insert(al);
        if (t->year > 0) {
            yearSum += t->year;
            ++yearCount;
        }
    }
    p.year = yearCount > 0 ? yearSum / yearCount : 0.0;
    return p;
}

ScoreParts scoreTrack(const TrackFeatures& track, const SeedProfile& seed, const TagRelations& relations, qint64 nowMs, const RecommenderConfig& config)
{
    ScoreParts s;
    if (!track.tags.isEmpty() && !seed.tags.isEmpty()) {
        int shared = 0;
        for (int tag : track.tags)
            if (seed.tags.contains(tag))
                ++shared;
        const int unionSize = static_cast<int>(track.tags.size() + seed.tags.size()) - shared;
        s.tags = config.sharedTags * (unionSize > 0 ? double(shared) / unionSize : 0.0);

        bool related = false, opposite = false;
        for (int tag : track.tags) {
            for (int st : seed.tags) {
                related = related || relations.isRelated(st, tag);
                opposite = opposite || relations.isOpposite(st, tag);
            }
        }
        if (related)
            s.related = config.relatedGenre;
        if (opposite)
            s.opposite = config.oppositeGenre;
    }
    for (qint64 a : track.artists)
        if (seed.artists.contains(a)) {
            s.artist = config.sameArtist;
            break;
        }
    if (track.year > 0 && seed.year > 0)
        s.year = config.yearProximity * std::max(0.0, 1.0 - std::abs(track.year - seed.year) / config.yearSpan);
    if (track.lastPlayedMs > 0 && nowMs >= track.lastPlayedMs) {
        const double age = double(nowMs - track.lastPlayedMs) / double(config.recentDecayMs);
        if (age < 1.0)
            s.recent = config.recentlyPlayed * (1.0 - age);
    }
    return s;
}

void Selector::setPool(QList<TrackFeatures> pool, TagRelations relations)
{
    m_pool = std::move(pool);
    m_relations = std::move(relations);
    m_index.clear();
    m_index.reserve(m_pool.size());
    for (int i = 0; i < m_pool.size(); ++i)
        m_index.insert(m_pool.at(i).id.value, i);
}

// relaxation: 0 = outside the seed albums and not recommended before, 1 = seed albums allowed, 2 = recommended-before allowed
QList<const TrackFeatures*> Selector::candidates(const PickRequest& request, const SeedProfile& seed, int relaxation) const
{
    QSet<qint64> excluded;
    for (TrackId id : request.exclude)
        excluded.insert(id.value);
    QList<const TrackFeatures*> out;
    for (const TrackFeatures& t : m_pool) {
        if (excluded.contains(t.id.value))
            continue;
        if (relaxation < 2 && m_used.contains(t.id.value))
            continue;
        if (relaxation < 1) {
            bool inSeed = false;
            for (qint64 al : t.albums)
                if (seed.albums.contains(al)) {
                    inSeed = true;
                    break;
                }
            if (inSeed)
                continue;
        }
        out.append(&t);
    }
    return out;
}

bool Selector::allowed(const TrackFeatures& t, const QList<qint64>& planned, int level) const
{
    if (t.album == 0 || level >= 2)
        return true;
    if (!planned.isEmpty() && planned.last() == t.album)
        return false;  // never the same album twice in a row
    if (level == 1)
        return true;
    int count = 0;
    const int from = std::max<int>(0, static_cast<int>(planned.size()) - (m_config.varietyWindow - 1));
    for (int i = from; i < planned.size(); ++i)
        if (planned.at(i) == t.album)
            ++count;
    return count < m_config.maxPerAlbumInWindow;
}

QList<TrackId> Selector::pick(const PickRequest& request)
{
    QList<const TrackFeatures*> seeds;
    for (TrackId id : request.seeds)
        if (auto it = m_index.constFind(id.value); it != m_index.constEnd())
            seeds.append(&m_pool.at(it.value()));
    const SeedProfile seed = SeedProfile::of(seeds);

    QList<const TrackFeatures*> pool;
    for (int relaxation = 0; relaxation <= 2; ++relaxation) {
        if (relaxation == 2)
            m_used.clear();  // everything was recommended once: start over
        pool = candidates(request, seed, relaxation);
        if (pool.size() >= m_config.minPool)
            break;
    }
    if (pool.size() < m_config.minPool)
        return {};
    for (int i = 0; i < m_config.maxScored && i < pool.size() - 1 && pool.size() > m_config.maxScored; ++i)
        std::swap(pool[i], pool[i + static_cast<int>(m_random.next01() * (pool.size() - i))]);  // partial shuffle: a random sample first
    if (pool.size() > m_config.maxScored)
        pool.resize(m_config.maxScored);

    struct Scored {
        const TrackFeatures* t;
        double score;
    };
    QList<Scored> scored;
    scored.reserve(pool.size());
    for (const TrackFeatures* t : pool)
        scored.append({t, scoreTrack(*t, seed, m_relations, request.nowMs, m_config).total()});
    const int keep = std::min<int>(m_config.topCandidates, static_cast<int>(scored.size()));
    std::partial_sort(scored.begin(), scored.begin() + keep, scored.end(), [](const Scored& a, const Scored& b) {
        return a.score != b.score ? a.score > b.score : a.t->id.value < b.t->id.value;
    });
    scored.resize(keep);

    // what came just before: the earlier picks, or (the first time) the tracks that are queued / playing now
    QList<qint64> planned(m_recentAlbums.begin(), m_recentAlbums.end());
    if (planned.isEmpty())
        for (TrackId id : request.exclude)
            if (auto it = m_index.constFind(id.value); it != m_index.constEnd())
                planned.append(m_pool.at(it.value()).album);

    QList<TrackId> out;
    while (out.size() < request.count && !scored.isEmpty()) {
        QList<int> options;
        for (int level = 0; level <= 2 && options.isEmpty(); ++level)
            for (int i = 0; i < scored.size(); ++i)
                if (allowed(*scored.at(i).t, planned, level))
                    options.append(i);
        double lowest = scored.at(options.first()).score;
        for (int i : options)
            lowest = std::min(lowest, scored.at(i).score);
        double total = 0;
        for (int i : options)
            total += scored.at(i).score - lowest + 0.01;
        double r = m_random.next01() * total;
        int chosen = options.last();
        for (int i : options) {
            r -= scored.at(i).score - lowest + 0.01;
            if (r < 0) {
                chosen = i;
                break;
            }
        }
        const TrackFeatures* t = scored.at(chosen).t;
        out.append(t->id);
        planned.append(t->album);
        m_used.insert(t->id.value);
        scored.removeAt(chosen);
    }
    m_recentAlbums.assign(planned.end() - std::min<qsizetype>(planned.size(), m_config.varietyWindow), planned.end());
    return out;
}

QList<Selector::Candidate> Selector::top(const PickRequest& request, int n) const
{
    QList<const TrackFeatures*> seeds;
    for (TrackId id : request.seeds)
        if (auto it = m_index.constFind(id.value); it != m_index.constEnd())
            seeds.append(&m_pool.at(it.value()));
    const SeedProfile seed = SeedProfile::of(seeds);
    QList<const TrackFeatures*> pool;
    for (int relaxation = 0; relaxation <= 2; ++relaxation) {
        pool = candidates(request, seed, relaxation);
        if (pool.size() >= m_config.minPool)
            break;
    }
    QList<Candidate> out;
    for (const TrackFeatures* t : pool)
        out.append({t->id, scoreTrack(*t, seed, m_relations, request.nowMs, m_config), t->album});
    std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) { return a.parts.total() > b.parts.total(); });
    if (out.size() > n)
        out.resize(n);
    return out;
}

}  // namespace semplice
