#pragma once

#include "core/db/Database.h"
#include "core/domain/Types.h"

namespace semplice {

enum class TagOwner { Artist, Album, Track };

// Stores canonical tag keys only. Validation against the vocabulary is the service's job.
class TagRepo {
public:
    explicit TagRepo(Database& db) : m_db(db) {}

    Result<QStringList> tags(TagOwner owner, qint64 id);  // sorted
    Result<void> setTags(TagOwner owner, qint64 id, const QStringList& keys);  // replaces
    Result<void> addTags(TagOwner owner, qint64 id, const QStringList& keys);

private:
    Database& m_db;
};

class RecentRepo {
public:
    explicit RecentRepo(Database& db) : m_db(db) {}

    static constexpr int kMaxItems = 50;

    // Moves the item to the front (no duplicates) and trims the list to 50.
    Result<void> touch(ContainerKind kind, qint64 id, qint64 playedAt);
    Result<void> remove(ContainerKind kind, qint64 id);
    Result<QList<Membership>> list();  // newest first; Membership.addedAt = played_at, position = rank

private:
    Database& m_db;
};

class KvRepo {
public:
    explicit KvRepo(Database& db) : m_db(db) {}

    Result<std::optional<QString>> get(const QString& key);
    Result<void> set(const QString& key, const QString& value);
    Result<void> remove(const QString& key);

private:
    Database& m_db;
};

}  // namespace semplice
