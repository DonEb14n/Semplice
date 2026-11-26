#pragma once

#include "core/db/Database.h"
#include "core/domain/Types.h"

namespace semplice {

class TrackRepo {
public:
    explicit TrackRepo(Database& db) : m_db(db) {}

    Result<TrackId> insert(const Track& track);  // credits are separate (setCredits)
    Result<std::optional<Track>> get(TrackId id);
    Result<std::optional<Track>> findByHash(const QByteArray& hash);
    Result<std::optional<Track>> findByPath(const QString& relativePath);
    Result<bool> pathTaken(const QString& relativePath);
    Result<void> setHome(TrackId id, HomeRef home, const QString& filePath, qint64 now);
    Result<void> remove(TrackId id);  // cascades memberships, credits, tags

    Result<QList<TrackCredit>> credits(TrackId id);  // position order
    Result<void> setCredits(TrackId id, const QList<TrackCredit>& credits);
    Result<void> addCredit(TrackId id, TrackCredit credit);  // ignored if the artist is already credited

    // All albums and playlists that contain the track, oldest membership first.
    Result<QList<Membership>> memberships(TrackId id);
    Result<int> membershipCount(TrackId id);

    Result<int> creditCount(ArtistId artist);
    Result<QList<TrackId>> creditedTo(ArtistId artist);
    // Virtual "Unsorted tracks" of a hidden artist: tracks credited as main that are in a playlist.
    Result<QList<TrackId>> unsortedOfHiddenArtist(ArtistId artist);

    Result<void> recordPlay(TrackId id, qint64 now);

    // Tag-sync marker (migration 0003) and file facts.
    Result<void> setTagsDirty(TrackId id, bool dirty);
    Result<QList<TrackId>> dirtyTracks();
    Result<void> setFileInfo(TrackId id, qint64 size, qint64 mtime, qint64 now);
    Result<void> setMedia(TrackId id, qint64 durationMs, const QString& codec, bool hasEmbeddedCover);
    Result<void> setTitle(TrackId id, const QString& title, qint64 now);

    struct FileRow {
        TrackId id;
        QString path;
        qint64 size = 0;
        qint64 mtime = 0;
    };
    Result<QList<FileRow>> allFiles();  // for Rescan
    Result<int> count();

private:
    Database& m_db;
};

}  // namespace semplice
