#pragma once

#include "core/db/Database.h"
#include "core/playback/PlaybackInterfaces.h"
#include "core/recommend/Recommender.h"
#include "core/tags/GenreVocabulary.h"
#include "core/util/Clock.h"

#include <future>
#include <memory>
#include <mutex>

namespace semplice {

struct RecommendationPool {
    QList<TrackFeatures> tracks;
    TagRelations relations;
};

// Reads every track's features with a few bulk queries (any read connection).
Result<RecommendationPool> loadRecommendationPool(Database& db, const GenreVocabulary& genres);

// The recommender the player uses. The pool is built on a worker thread when a library opens and again after
// the library changed; `pick` runs on the caller's thread against memory only (it waits for the pool if the
// first build is not finished yet).
class LibraryRecommendationSource final : public IRecommendationSource {
public:
    LibraryRecommendationSource(QString dbPath, GenreVocabulary genres, const IClock& clock, quint64 seed, RecommenderConfig config = {});
    ~LibraryRecommendationSource() override;

    // Starts (re)building the pool in the background.
    void refresh();
    QList<TrackId> pick(const RecommendationRequest& request) override;

private:
    void collect();  // takes a finished build into the selector

    QString m_dbPath;
    GenreVocabulary m_genres;
    const IClock& m_clock;
    MersenneRandom m_random;
    Selector m_selector;
    std::future<RecommendationPool> m_build;
    bool m_building = false;
    bool m_dirty = false;  // a change arrived while a pool was being built
};

}  // namespace semplice
