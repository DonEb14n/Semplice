#pragma once

#include "core/home/CategoryTypes.h"

#include <QHash>
#include <QSet>

#include <random>

namespace semplice {

// What the generator needs to know about the library.
class ICategorySource {
public:
    virtual ~ICategorySource() = default;
    struct Instance {
        QString arg;
        QString label;
    };
    // Every instance of `type` with enough content to be shown (see the k... limits of CategoryGenerator).
    virtual QList<Instance> instances(CategoryType type) = 0;
};

// Chooses the carousels below "Recently Played" (SPEC 8.6): no type repeats until every eligible type has
// been used once, then the cycle starts again; a type picks an instance that was not shown yet.
class CategoryGenerator {
public:
    static constexpr int kInitialCount = 5;
    static constexpr int kMaxItems = 50;
    static constexpr int kMinArtistAlbums = 3;
    static constexpr int kMinAlbums = 5;  // year, decade, genre and "Your Albums"
    static constexpr int kMinPlaylists = 2;

    CategoryGenerator(ICategorySource& source, quint64 seed) : m_source(source), m_rng(seed) {}

    // Up to `count` new categories; fewer (or none) when the content is used up.
    QList<CategorySpec> next(int count);
    void reset();

private:
    ICategorySource& m_source;
    std::mt19937_64 m_rng;
    QSet<QString> m_shown;     // keys already generated
    QSet<int> m_typesInCycle;  // types used since the cycle began
};

}  // namespace semplice
