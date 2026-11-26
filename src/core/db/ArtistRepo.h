#pragma once

#include "core/db/Database.h"
#include "core/domain/Types.h"

namespace semplice {

// Plain persistence for artists. Business rules (visibility, GC, tag seeding) live in LibraryService.
class ArtistRepo {
public:
    explicit ArtistRepo(Database& db) : m_db(db) {}

    // Fills id; name_key is derived from `artist.name`. Fails with E_ALREADY_EXISTS on a duplicate name.
    Result<ArtistId> insert(const Artist& artist);
    Result<std::optional<Artist>> get(ArtistId id);
    Result<std::optional<Artist>> findByName(const QString& name);  // identity comparison
    Result<QList<Artist>> all();
    Result<QList<Artist>> visible();

    Result<void> setUserHidden(ArtistId id, bool userHidden, qint64 now);
    Result<void> setVisibility(ArtistId id, bool hidden, const QString& dirName, qint64 now);
    Result<void> setTagsSeeded(ArtistId id, bool seeded);
    Result<void> setName(ArtistId id, const QString& name, const QString& dirName, qint64 now);  // dirName empty = keep NULL (hidden)
    Result<void> setAvatar(ArtistId id, const QString& avatarPath, qint64 now);
    Result<void> remove(ArtistId id);

    Result<bool> dirNameTaken(const QString& dirName);

private:
    Database& m_db;
};

}  // namespace semplice
