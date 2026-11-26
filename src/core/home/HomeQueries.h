#pragma once

#include "core/db/Database.h"
#include "core/home/CategoryGenerator.h"

#include <random>

namespace semplice {

// The Home page's questions to the database (engine thread). Also the real ICategorySource.
class HomeQueries final : public ICategorySource {
public:
    explicit HomeQueries(Database& db) : m_db(db) {}

    QList<Instance> instances(CategoryType type) override;

    // Albums and playlists the user played lately, newest first, at most 50 (SPEC 7.4).
    Result<QList<CarouselItem>> recent(int limit = 50);
    // The items of a category. limit > 0: what the carousel shows (artist, year, decade, genre: a random sample
    // in random order; playlists and albums: the most recent ones); limit == 0: ALL items for the category page
    // (artist: newest release first; the others: most recently added first). `rng` is only needed for limit > 0.
    Result<CategoryContents> contents(const CategorySpec& spec, int limit, std::mt19937_64* rng = nullptr);
    // True if the library holds any album or playlist (decides between "Welcome" and the occasion greetings).
    Result<bool> hasContent();

    // Albums matching a WHERE clause on table alias `a` (also used by the cards). `order` is an ORDER BY list.
    Result<QList<CarouselItem>> albums(const QString& where, const QVariantList& params, const QString& order);

private:
    Result<qint64> albumDuration(const QString& where, const QVariantList& params);
    Database& m_db;
};

}  // namespace semplice
