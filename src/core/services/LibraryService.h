#pragma once

#include "core/db/AlbumRepo.h"
#include "core/db/ArtistRepo.h"
#include "core/db/PlaylistRepo.h"
#include "core/db/TagRepo.h"
#include "core/db/TrackRepo.h"
#include "core/services/IPathNamer.h"
#include "core/services/LibraryTypes.h"
#include "core/services/TagVocabulary.h"
#include "core/storage/IFileStorage.h"
#include "core/tags/GenreVocabulary.h"
#include "core/tags/TagService.h"
#include "core/util/Clock.h"
#include "core/util/Job.h"

#include <QObject>

#include <functional>
#include <memory>

namespace semplice {

// What the service needs to touch real files. All optional: without them (the M1 rule tests) it only
// does database work and records the file operations.
struct LibraryEnvironment {
    ITagService* tags = nullptr;               // write tags into the library copies (SPEC 6.2)
    const GenreVocabulary* genres = nullptr;   // English genre names for the file tags
    QString libraryRoot;                       // absolute; needed to read covers and to check files
};

// The library rules of SPEC 5 and 6.4 as operations. Every public mutating call is ONE database
// transaction plus ONE journaled file operation (SPEC 4.5):
//
//   rows change -> journal written -> files copied/moved -> new files tagged -> COMMIT -> Recycle Bin
//
// A failure or cancel before the commit undoes both sides; a crash is repaired at the next start.
// Tags of existing files are synced AFTER the commit and never roll anything back: a failed write
// leaves `track.tags_dirty` set and Rescan retries it.
//
// Not thread-safe and not re-entrant: one operation at a time on one Database (the UI runs the
// service on a single worker thread).
class LibraryService : public QObject {
    Q_OBJECT
public:
    LibraryService(Database& db, IFileStorage& storage, const IPathNamer& namer, const IClock& clock,
                   TagVocabulary vocabulary, LibraryEnvironment environment = {}, QObject* parent = nullptr);
    ~LibraryService() override;

    // Full outcome of the most recent mutating call (also available for calls returning ids).
    const OperationReport& lastReport() const { return m_lastReport; }

    // ---- creation ----
    Result<ArtistId> createArtist(const ArtistDraft& draft);
    Result<AlbumId> createAlbum(const AlbumDraft& draft, const JobContext& job = {});
    Result<PlaylistId> createPlaylist(const PlaylistDraft& draft, const JobContext& job = {});
    // New files added to an existing album / playlist (import onto an open page, Edit window "add tracks").
    Result<void> appendTracksToAlbum(AlbumId album, const QList<TrackInput>& tracks, const JobContext& job = {});
    Result<void> appendTracksToPlaylist(PlaylistId playlist, const QList<TrackInput>& tracks, const JobContext& job = {});

    // ---- editing (SPEC 4.3: folders follow names) ----
    Result<OperationReport> updateArtist(ArtistId artist, const ArtistPatch& patch, const JobContext& job = {});
    // Everything of `source` (albums, co-authorships, credits, tags, Unsorted tracks) becomes `target`'s and `source` is
    // deleted. updateArtist does this by itself when an artist is renamed to the name of another one: the artist being
    // renamed wins (its albums keep their names; a clash on the other side gets " (2)").
    Result<void> mergeArtistInto(ArtistId source, ArtistId target, const JobContext& job = {});
    Result<OperationReport> updateAlbum(AlbumId album, const AlbumPatch& patch, const JobContext& job = {});
    Result<OperationReport> updatePlaylist(PlaylistId playlist, const PlaylistPatch& patch, const JobContext& job = {});
    Result<OperationReport> updateTrack(TrackId track, const TrackPatch& patch, const JobContext& job = {});

    // ---- memberships ----
    Result<void> addTrackToAlbum(TrackId track, AlbumId album);
    Result<void> addTrackToPlaylist(TrackId track, PlaylistId playlist);
    // Only allowed while the track exists elsewhere (UI doc p.38); otherwise E_NOT_ALLOWED.
    Result<void> removeTrackFromAlbum(TrackId track, AlbumId album);
    Result<void> removeTrackFromPlaylist(TrackId track, PlaylistId playlist);

    // ---- "Unsorted tracks" (SPEC 5.2) ----
    // Puts a track into the artist's real Unsorted album (creating it, making the artist visible, and
    // crediting the artist if needed). A playlist-homed file moves into the Unsorted folder.
    Result<AlbumId> addToDiscography(TrackId track, ArtistId artist);
    // Same for every track in the hidden artist's virtual Unsorted list.
    Result<AlbumId> materialiseHiddenArtist(ArtistId artist);

    // ---- deletion (SPEC 6.4). plan*() = the real operation, executed and rolled back ----
    Result<DeletePlan> planDeleteTrack(TrackId track);
    Result<OperationReport> deleteTrack(TrackId track);
    Result<DeletePlan> planDeleteAlbum(AlbumId album);
    Result<OperationReport> deleteAlbum(AlbumId album);
    Result<DeletePlan> planDeletePlaylist(PlaylistId playlist);
    Result<OperationReport> deletePlaylist(PlaylistId playlist);
    Result<DeletePlan> planDeleteArtist(ArtistId artist);
    Result<OperationReport> deleteArtist(ArtistId artist);

    // ---- tag sync ----
    // Writes the file tags of every track with tags_dirty set (Rescan, start-up). Returns the tracks
    // that still failed (locked, read-only, ...); successful ones are cleared.
    // `giveUp` (optional): tracks that failed before are skipped, and tracks that fail now for a reason that will not go away
    // by waiting (not "the file is playing") are added, so a file that cannot be written is not copied again and again.
    Result<QList<TrackId>> syncDirtyTags(const JobContext& job = {}, QSet<qint64>* giveUp = nullptr);
    // The tags a track's file should carry according to the database (also used by tests).
    Result<TagWrite> buildTagWrite(TrackId track);

    struct Context;  // per-operation scratch state (report, artists to re-evaluate); internal

signals:
    // Emitted after a successful commit (never for plans).
    void libraryChanged(const semplice::ChangeSet& changes);

private:
    // Runs `body` inside a transaction + journaled file operation. dryRun = roll back, touch no files.
    Result<OperationReport> transact(bool dryRun, const std::function<Result<void>()>& body, const JobContext& job = {});

    // validation
    Result<void> validateTags(const QStringList& tags) const;
    Result<void> validateTitle(const QString& title) const;
    Result<void> validateTracks(const QList<TrackInput>& tracks, bool needArtists) const;

    // naming / paths
    QString uniqueArtistDir(const QString& name, ArtistId except = {});
    QString uniqueAlbumDir(ArtistId primary, const QString& title, AlbumId except = {});
    QString uniquePlaylistDir(const QString& name, PlaylistId except = {});
    Result<QString> albumFolder(const Album& album);
    Result<QString> expectedTrackPath(const Track& track, const QSet<QString>& reserved);
    Result<void> syncTrackPaths(const QList<TrackId>& tracks, bool recordFileMoves);
    Result<void> syncContainerPaths(ContainerKind kind, qint64 id, bool recordFileMoves);

    // building blocks (all assume an open transaction and a live m_ctx)
    Result<ArtistId> findOrCreateArtist(ArtistId id, const QString& name, bool asAuthor);
    Result<TrackId> resolveTrack(const TrackInput& input, bool defaultCreditPrimary, ArtistId primary, HomeRef home,
                                 bool& isNew);
    Result<void> placeNewTrack(TrackId id, const TrackDraft& draft);
    Result<void> addNewTracks(ContainerKind kind, qint64 containerId, const QList<TrackInput>& tracks,
                              ArtistId defaultCredit, int& position);
    Result<void> changeHome(TrackId id, HomeRef home);
    Result<void> moveHomeToNext(TrackId id);
    Result<void> makeVisible(ArtistId id);
    Result<void> hideArtist(const Artist& artist);
    Result<Album> ensureUnsortedAlbum(ArtistId artist);
    Result<void> seedArtistTags(const QList<ArtistId>& artists, const QStringList& albumTags);
    Result<void> markAlbumLost(const Album& album);
    Result<void> addToDiscographyInternal(TrackId track, const Album& unsorted, const Artist& artist);
    // Cover files: copy `sourceImage` in as <folder>/<owner>_cover.<ext>, trash the old cover if the name differs.
    Result<QString> storeCover(const QString& folderRelative, const QString& ownerPrefix, const QString& sourceImage,
                               const QString& oldCoverPath);
    void markDirty(TrackId id);
    Result<void> markDirtyForAlbum(AlbumId id);
    Result<void> markDirtyForArtist(ArtistId id);
    Result<void> renameAlbumFolder(const Album& album, ArtistId newPrimary, const QString& newTitle);
    Result<void> relocateCovers(const Album& album);

    // deleting building blocks
    Result<void> eraseTrack(const Track& track);
    Result<void> releaseTracks(ContainerKind kind, qint64 id, const QList<TrackId>& members);
    Result<void> deleteTrackInternal(TrackId id);
    Result<void> deleteAlbumInternal(AlbumId id);
    Result<void> deletePlaylistInternal(PlaylistId id);
    Result<void> settleContainer(ContainerKind kind, qint64 id);
    Result<void> rehomePrimary(const Album& album);
    Result<void> reevaluateArtists();

    Result<OperationReport> doDeleteTrack(TrackId track, bool dry);
    Result<OperationReport> doDeleteAlbum(AlbumId album, bool dry);
    Result<OperationReport> doDeletePlaylist(PlaylistId playlist, bool dry);
    Result<OperationReport> doDeleteArtist(ArtistId artist, bool dry);

    // tag sync internals
    Result<void> writeTagsOf(TrackId id, bool newFile);
    Result<void> finishNewFiles(const QList<CopyResult>& copies, const QList<FileOp>& ops);
    void syncTouchedTags(OperationReport& report, const QSet<TrackId>& tracks);

    qint64 nowMs() const;
    QString absolute(const QString& relative) const;

    Database& m_db;
    IFileStorage& m_storage;
    const IPathNamer& m_namer;
    const IClock& m_clock;
    TagVocabulary m_vocabulary;
    LibraryEnvironment m_env;
    ArtistRepo m_artists;
    AlbumRepo m_albums;
    PlaylistRepo m_playlists;
    TrackRepo m_tracks;
    TagRepo m_tags;
    RecentRepo m_recent;

    std::unique_ptr<Context> m_ctx;
    OperationReport m_lastReport;
};

}  // namespace semplice
