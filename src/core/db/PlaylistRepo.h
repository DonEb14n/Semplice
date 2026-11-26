#pragma once

#include "core/db/Database.h"
#include "core/domain/Types.h"

namespace semplice {

class PlaylistRepo {
public:
    explicit PlaylistRepo(Database& db) : m_db(db) {}

    Result<PlaylistId> insert(const Playlist& playlist);  // E_ALREADY_EXISTS on duplicate name
    Result<std::optional<Playlist>> get(PlaylistId id);
    Result<std::optional<Playlist>> findByName(const QString& name);
    Result<QList<Playlist>> all();
    Result<void> setCover(PlaylistId id, const QString& coverPath, qint64 now);
    Result<void> setName(PlaylistId id, const QString& name, const QString& dirName, qint64 now);
    Result<void> reorder(PlaylistId id, const QList<TrackId>& order);
    Result<void> remove(PlaylistId id);
    Result<bool> dirNameTaken(const QString& dirName);

    Result<QList<TrackId>> tracks(PlaylistId id);  // in position order
    Result<int> trackCount(PlaylistId id);
    Result<void> addTrack(PlaylistId id, TrackId track, int position, qint64 addedAt);
    Result<void> removeTrack(PlaylistId id, TrackId track);
    Result<void> renumber(PlaylistId id);
    Result<int> positionOf(PlaylistId id, TrackId track);

private:
    Database& m_db;
};

}  // namespace semplice
