#include "core/services/LibraryService.h"

#include "core/services/LibraryServiceInternal.h"
#include "core/storage/DbCommitLog.h"
#include "core/tags/CoverDiscovery.h"
#include "core/util/Logger.h"
#include "core/util/Normalizer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace semplice {

using namespace internal;

LibraryService::LibraryService(Database& db, IFileStorage& storage, const IPathNamer& namer, const IClock& clock,
                               TagVocabulary vocabulary, LibraryEnvironment environment, QObject* parent)
    : QObject(parent),
      m_db(db),
      m_storage(storage),
      m_namer(namer),
      m_clock(clock),
      m_vocabulary(std::move(vocabulary)),
      m_env(std::move(environment)),
      m_artists(db),
      m_albums(db),
      m_playlists(db),
      m_tracks(db),
      m_tags(db),
      m_recent(db)
{
}

LibraryService::~LibraryService() = default;

qint64 LibraryService::nowMs() const
{
    return m_clock.now().toMSecsSinceEpoch();
}

// ---------------------------------------------------------------------------------------------
// Transaction wrapper
// ---------------------------------------------------------------------------------------------

namespace {

// A "Recycle Bin" step whose path is used again later in the same operation (a delete followed by a
// renumbering rename onto the freed name) would collide, because deletions only run after the commit.
// Such a step becomes "move aside now" (undoable) + "Recycle Bin later".
QList<FileOp> rewriteTrashConflicts(const QList<FileOp>& ops, const std::function<bool(const QString&)>& isDirectory)
{
    auto key = [](const QString& p) { return PathSanitizer::compareKey(p); };
    QList<FileOp> now;
    QList<FileOp> later;
    int counter = 0;
    for (int i = 0; i < ops.size(); ++i) {
        const FileOp& op = ops.at(i);
        bool conflict = false;
        if (op.kind == FileOp::Kind::Trash) {
            const QString k = key(op.from);
            for (int j = i + 1; j < ops.size() && !conflict; ++j) {
                const FileOp& next = ops.at(j);
                if (next.kind != FileOp::Kind::Copy && next.kind != FileOp::Kind::MoveFile && next.kind != FileOp::Kind::MoveDir)
                    continue;
                const QString target = key(next.to);
                conflict = target == k || target.startsWith(k + QLatin1Char('/'));
            }
        }
        if (!conflict) {
            now.append(op);
            continue;
        }
        const QString aside = op.from + QStringLiteral(".semplice-del-") + QString::number(++counter);
        now.append({isDirectory(op.from) ? FileOp::Kind::MoveDir : FileOp::Kind::MoveFile, op.from, aside});
        later.append({FileOp::Kind::Trash, aside, {}});
    }
    return now + later;
}

}  // namespace

QString LibraryService::absolute(const QString& relative) const
{
    return QDir(m_env.libraryRoot).filePath(relative);
}

Result<OperationReport> LibraryService::transact(bool dryRun, const std::function<Result<void>()>& body, const JobContext& job)
{
    if (job.cancelled())
        return Error::make(ErrorCode::Cancelled);

    m_ctx = std::make_unique<Context>();
    m_ctx->job = &job;
    struct Drop {
        LibraryService* self;
        ~Drop() { self->m_ctx.reset(); }
    } drop{this};

    Transaction tx(m_db);
    SEMPLICE_TRY(tx.begin());
    SEMPLICE_TRY(body());
    SEMPLICE_TRY(reevaluateArtists());

    // Tracks whose file tags must change are marked INSIDE the transaction: a crash or a locked file
    // later leaves the marker behind and Rescan retries (SPEC 6.2).
    QSet<TrackId> dirty;
    for (TrackId t : std::as_const(m_ctx->dirty)) {
        SEMPLICE_ASSIGN(exists, m_tracks.get(t));
        if (!exists)
            continue;
        SEMPLICE_TRY(m_tracks.setTagsDirty(t, true));
        dirty.insert(t);
    }

    OperationReport& report = m_ctx->report;
    sortList(report.tracksDeleted);
    sortList(report.tracksKept);
    sortList(report.tracksMoved);
    sortList(report.tracksCreated);
    sortList(report.tracksReused);
    sortList(report.albumsDeleted);
    sortList(report.playlistsDeleted);
    sortList(report.albumsRehomed);
    sortList(report.artistsDeleted);
    sortList(report.artistsHidden);
    sortList(report.artistsMadeVisible);

    if (dryRun) {
        tx.rollback();
        return report;
    }

    // ---- phase A: files (journaled) ----
    QList<FileOp> ops = rewriteTrashConflicts(report.fileOps, [this](const QString& rel) {
        return !m_env.libraryRoot.isEmpty() && QFileInfo(absolute(rel)).isDir();
    });
    report.fileOps = ops;

    std::unique_ptr<IStorageOperation> operation;
    if (!ops.isEmpty()) {
        auto begun = m_storage.begin(ops);
        if (!begun.ok())
            return begun.error();
        operation = std::move(begun).value();

        ExecuteOptions options;
        options.cancel = job.cancel;
        options.progress = [&job](const CopyProgress& p) { job.report(QStringLiteral("copy"), p.bytesDone, p.bytesTotal); };
        auto executed = operation->execute(options);
        if (!executed.ok())
            return executed.error();  // the storage undid its steps; `tx` rolls the rows back

        auto abort = [&](const Error& e) -> Result<OperationReport> {
            if (e.code != ErrorCode::CrashInjected)
                (void)operation->rollback();
            return e;
        };
        if (auto r = finishNewFiles(executed.value(), ops); !r.ok())
            return abort(r.error());
        if (auto r = DbCommitLog::mark(m_db, operation->id(), nowMs()); !r.ok())
            return abort(r.error());
    } else if (auto r = finishNewFiles({}, ops); !r.ok()) {
        return r.error();
    }

    // ---- commit ----
    if (auto r = m_storage.checkpoint(QStringLiteral("db:before-commit")); !r.ok())
        return r.error();  // simulated power loss: neither side cleans up
    if (auto r = tx.commit(); !r.ok()) {
        if (operation)
            (void)operation->rollback();
        return r.error();
    }
    if (auto r = m_storage.checkpoint(QStringLiteral("db:after-commit")); !r.ok())
        return r.error();

    // ---- phase B: Recycle Bin ----
    if (operation) {
        if (auto r = operation->finish(); !r.ok()) {
            if (r.error().code == ErrorCode::CrashInjected)
                return r.error();
            // The database is committed; the journal entry stays and the next start completes the deletions.
            report.warnings << QStringLiteral("deletions pending: ") + r.error().devMessage;
            SLOG_WARN(QStringLiteral("file operation not finished: ") + r.error().devMessage);
        }
    }

    OperationReport result = report;
    QSet<TrackId> toSync = dirty;
    for (const auto& nf : std::as_const(m_ctx->newFiles))
        toSync.remove(nf.id);  // written in place before the commit
    if (!result.changes.isEmpty())
        emit libraryChanged(result.changes);
    syncTouchedTags(result, toSync);
    m_lastReport = result;
    return result;
}

// New files are tagged in place and described in the database (size, time, duration, codec) before the
// commit; their Copy results are checked against the hash computed when the source was scanned.
Result<void> LibraryService::finishNewFiles(const QList<CopyResult>& copies, const QList<FileOp>& ops)
{
    for (const CopyResult& c : copies) {
        if (c.opIndex < 0 || c.opIndex >= ops.size())
            continue;
        for (const auto& nf : std::as_const(m_ctx->newFiles)) {
            if (nf.source == ops.at(c.opIndex).from && !nf.expectedHash.isEmpty() && nf.expectedHash != c.sha256)
                return Error::make(ErrorCode::InvalidArgument,
                                   QStringLiteral("the source file changed while it was imported: ") + nf.source);
        }
    }
    for (const auto& nf : std::as_const(m_ctx->newFiles)) {
        const QString path = absolute(nf.relativePath);
        if (m_env.libraryRoot.isEmpty() || !QFileInfo(path).isFile())
            continue;
        bool written = false;
        if (m_env.tags) {
            if (auto r = writeTagsOf(nf.id, true); r.ok()) {
                written = true;
            } else {
                addUnique(m_ctx->report.tagWriteFailed, nf.id);
                m_ctx->report.warnings << QStringLiteral("tags not written for %1: %2").arg(nf.relativePath, r.error().devMessage);
            }
            if (auto media = m_env.tags->read(path); media.ok())
                SEMPLICE_TRY(m_tracks.setMedia(nf.id, media.value().durationMs, media.value().codec, media.value().hasCover));
        }
        const QFileInfo info(path);
        SEMPLICE_TRY(m_tracks.setFileInfo(nf.id, info.size(), info.lastModified().toMSecsSinceEpoch(), nowMs()));
        if (written)
            SEMPLICE_TRY(m_tracks.setTagsDirty(nf.id, false));
    }
    return {};
}

void LibraryService::syncTouchedTags(OperationReport& report, const QSet<TrackId>& tracks)
{
    if (!m_env.tags || tracks.isEmpty())
        return;
    QList<TrackId> sorted(tracks.begin(), tracks.end());
    std::sort(sorted.begin(), sorted.end());
    for (TrackId id : std::as_const(sorted)) {
        if (m_ctx && m_ctx->job && m_ctx->job->cancelled())
            break;
        if (auto r = writeTagsOf(id, false); !r.ok()) {
            addUnique(report.tagWriteFailed, id);
            report.warnings << QStringLiteral("tags not written for track %1: %2").arg(id.value).arg(r.error().devMessage);
            SLOG_WARN(report.warnings.last());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------------------------

Result<void> LibraryService::validateTags(const QStringList& tags) const
{
    const QString unknown = m_vocabulary.firstUnknown(tags);
    if (!unknown.isEmpty())
        return Error::make(ErrorCode::UnknownTag, QStringLiteral("unknown tag key: ") + unknown);
    return {};
}

Result<void> LibraryService::validateTitle(const QString& title) const
{
    if (Normalizer::display(title).isEmpty())
        return Error::make(ErrorCode::NameEmpty);
    return {};
}

Result<void> LibraryService::validateTracks(const QList<TrackInput>& tracks, bool needArtists) const
{
    for (const TrackInput& input : tracks) {
        if (input.existing)
            continue;
        const TrackDraft& d = input.draft;
        SEMPLICE_TRY(validateTitle(d.title));
        if (d.contentHash.isEmpty())
            return Error::make(ErrorCode::InvalidArgument, QStringLiteral("track draft without content hash"));
        if (needArtists && d.credits.isEmpty())
            return Error::make(ErrorCode::NoArtist, QStringLiteral("track needs at least one artist"));
        SEMPLICE_TRY(validateTags(d.tags));
    }
    return {};
}

// ---------------------------------------------------------------------------------------------
// Names and paths
// ---------------------------------------------------------------------------------------------

QString LibraryService::uniqueArtistDir(const QString& name, ArtistId except)
{
    const QString base = m_namer.dirName(name, NameKind::Artist);
    QString own;
    if (except.isValid()) {
        auto self = m_artists.get(except);
        if (self.ok() && self.value())
            own = self.value()->dirName;
    }
    QString candidate = base;
    for (int n = 2;; ++n) {
        auto taken = m_artists.dirNameTaken(candidate);
        if (!taken.ok() || !taken.value() || candidate.compare(own, Qt::CaseInsensitive) == 0)
            return candidate;
        candidate = PathSanitizer::withCollisionSuffix(base, n, false);
    }
}

QString LibraryService::uniqueAlbumDir(ArtistId primary, const QString& title, AlbumId except)
{
    const QString base = m_namer.dirName(title, NameKind::Album);
    QString own;
    if (except.isValid()) {
        auto self = m_albums.get(except);
        if (self.ok() && self.value() && self.value()->primaryArtist == primary)
            own = self.value()->dirName;
    }
    QString candidate = base;
    for (int n = 2;; ++n) {
        auto taken = m_albums.dirNameTaken(primary, candidate);
        // "Unsorted tracks" is reserved for the system album.
        const bool reserved = candidate.compare(kUnsortedDirName, Qt::CaseInsensitive) == 0;
        if (!reserved && (!taken.ok() || !taken.value() || candidate.compare(own, Qt::CaseInsensitive) == 0))
            return candidate;
        candidate = PathSanitizer::withCollisionSuffix(base, n, false);
    }
}

QString LibraryService::uniquePlaylistDir(const QString& name, PlaylistId except)
{
    const QString base = m_namer.dirName(name, NameKind::Playlist);
    QString own;
    if (except.isValid()) {
        auto self = m_playlists.get(except);
        if (self.ok() && self.value())
            own = self.value()->dirName;
    }
    QString candidate = base;
    for (int n = 2;; ++n) {
        auto taken = m_playlists.dirNameTaken(candidate);
        if (!taken.ok() || !taken.value() || candidate.compare(own, Qt::CaseInsensitive) == 0)
            return candidate;
        candidate = PathSanitizer::withCollisionSuffix(base, n, false);
    }
}

Result<QString> LibraryService::albumFolder(const Album& album)
{
    SEMPLICE_ASSIGN(artist, m_artists.get(album.primaryArtist));
    if (!artist || artist->dirName.isEmpty())
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("album owner has no folder"));
    return kArtistsRoot + QLatin1Char('/') + artist->dirName + QLatin1Char('/') + album.dirName;
}

Result<QString> LibraryService::expectedTrackPath(const Track& track, const QSet<QString>& reserved)
{
    const QString ext = extensionOf(track.filePath);
    if (track.home.kind == ContainerKind::Album) {
        SEMPLICE_ASSIGN(album, m_albums.get(AlbumId(track.home.id)));
        if (!album)
            return Error::make(ErrorCode::NotFound, QStringLiteral("home album missing"));
        SEMPLICE_ASSIGN(position, m_albums.positionOf(album->id, track.id));
        if (position == 0)
            return Error::make(ErrorCode::InvalidArgument, QStringLiteral("track is not in its home album"));
        SEMPLICE_ASSIGN(folder, albumFolder(*album));
        return folder + QLatin1Char('/') + m_namer.albumTrackFileName(position, track.title, ext);
    }

    SEMPLICE_ASSIGN(playlist, m_playlists.get(PlaylistId(track.home.id)));
    if (!playlist)
        return Error::make(ErrorCode::NotFound, QStringLiteral("home playlist missing"));
    SEMPLICE_ASSIGN(credits, m_tracks.credits(track.id));
    QString artistName;
    if (!credits.isEmpty()) {
        SEMPLICE_ASSIGN(artist, m_artists.get(credits.first().artist));
        if (artist)
            artistName = artist->name;
    }
    const QString folder = kPlaylistsRoot + QLatin1Char('/') + playlist->dirName;
    const QString fileName = m_namer.playlistTrackFileName(track.title, artistName, ext);
    QString candidate = folder + QLatin1Char('/') + fileName;
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot < 0 ? fileName : fileName.left(dot);
    const QString suffix = dot < 0 ? QString() : fileName.mid(dot);
    for (int n = 2;; ++n) {
        SEMPLICE_ASSIGN(owner, m_tracks.findByPath(candidate));
        const bool takenByOther = owner && owner->id != track.id;
        if (!takenByOther && !reserved.contains(candidate.toLower()))
            return candidate;
        candidate = QStringLiteral("%1/%2 (%3)%4").arg(folder, stem).arg(n).arg(suffix);
    }
}

Result<void> LibraryService::syncTrackPaths(const QList<TrackId>& ids, bool recordFileMoves)
{
    struct Change {
        Track track;
        QString newPath;
    };
    QList<Change> changes;
    QSet<QString> reserved;
    for (TrackId id : ids) {
        SEMPLICE_ASSIGN(track, m_tracks.get(id));
        if (!track)
            continue;
        SEMPLICE_ASSIGN(path, expectedTrackPath(*track, reserved));
        reserved.insert(path.toLower());
        if (path.compare(track->filePath, Qt::CaseSensitive) != 0)
            changes.append({*track, path});
    }
    if (changes.isEmpty())
        return {};

    // Two phases so that shifting positions never collides with a not-yet-updated sibling.
    const qint64 now = nowMs();
    for (const Change& c : changes)
        SEMPLICE_TRY(m_tracks.setHome(c.track.id, c.track.home, tempPath(c.track.id.value), now));
    for (const Change& c : changes) {
        SEMPLICE_TRY(m_tracks.setHome(c.track.id, c.track.home, c.newPath, now));
        touch(*m_ctx, c.track.id);
        m_ctx->dirty.insert(c.track.id);
        if (recordFileMoves)
            m_ctx->report.fileOps.append({FileOp::Kind::MoveFile, c.track.filePath, c.newPath});
    }
    return {};
}

Result<void> LibraryService::syncContainerPaths(ContainerKind kind, qint64 id, bool recordFileMoves)
{
    QList<TrackId> members;
    if (kind == ContainerKind::Album) {
        SEMPLICE_ASSIGN(list, m_albums.tracks(AlbumId(id)));
        members = list;
    } else {
        SEMPLICE_ASSIGN(list, m_playlists.tracks(PlaylistId(id)));
        members = list;
    }
    QList<TrackId> homed;
    for (TrackId t : members) {
        SEMPLICE_ASSIGN(track, m_tracks.get(t));
        if (track && track->home == HomeRef{kind, id})
            homed.append(t);
    }
    return syncTrackPaths(homed, recordFileMoves);
}

// ---------------------------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------------------------

Result<void> LibraryService::makeVisible(ArtistId id)
{
    SEMPLICE_ASSIGN(artist, m_artists.get(id));
    if (!artist)
        return Error::make(ErrorCode::NotFound, QStringLiteral("artist"));
    if (!artist->hidden)
        return {};
    const QString dir = uniqueArtistDir(artist->name);
    SEMPLICE_TRY(m_artists.setVisibility(id, false, dir, nowMs()));
    m_ctx->report.fileOps.append({FileOp::Kind::EnsureDir, kArtistsRoot + QLatin1Char('/') + dir, {}});
    addUnique(m_ctx->report.artistsMadeVisible, id);
    touch(*m_ctx, id);
    return {};
}

Result<void> LibraryService::hideArtist(const Artist& artist)
{
    SEMPLICE_TRY(m_artists.setVisibility(artist.id, true, QString(), nowMs()));
    if (!artist.dirName.isEmpty())
        m_ctx->report.fileOps.append({FileOp::Kind::Trash, kArtistsRoot + QLatin1Char('/') + artist.dirName, {}});
    addUnique(m_ctx->report.artistsHidden, artist.id);
    touch(*m_ctx, artist.id);
    return {};
}

Result<Album> LibraryService::ensureUnsortedAlbum(ArtistId artistId)
{
    SEMPLICE_ASSIGN(existing, m_albums.unsortedOf(artistId));
    if (existing)
        return *existing;

    SEMPLICE_TRY(makeVisible(artistId));
    const QDate today = m_clock.now().date();
    Album album;
    album.primaryArtist = artistId;
    album.dirName = kUnsortedDirName;
    album.type = AlbumType::Other;
    album.isUnsorted = true;
    album.date = {today.year(), today.month(), today.day()};
    album.createdAt = album.updatedAt = nowMs();
    SEMPLICE_ASSIGN(id, m_albums.insert(album));
    album.id = id;
    touch(*m_ctx, id);
    touch(*m_ctx, artistId);
    SEMPLICE_ASSIGN(folder, albumFolder(album));
    m_ctx->report.fileOps.append({FileOp::Kind::EnsureDir, folder, {}});
    return album;
}

Result<void> LibraryService::seedArtistTags(const QList<ArtistId>& artists, const QStringList& albumTags)
{
    if (albumTags.isEmpty())
        return {};
    for (ArtistId id : artists) {
        SEMPLICE_ASSIGN(artist, m_artists.get(id));
        if (!artist || artist->tagsSeeded)
            continue;
        SEMPLICE_TRY(m_tags.addTags(TagOwner::Artist, id.value, albumTags));
        SEMPLICE_TRY(m_artists.setTagsSeeded(id, true));
        touch(*m_ctx, id);
    }
    return {};
}

// An existing artist by id, else by name (any state), else created: hidden for a mere credit, visible
// for an album author. Called inside the operation's transaction, so a failed import leaves no artist behind.
Result<ArtistId> LibraryService::findOrCreateArtist(ArtistId id, const QString& name, bool asAuthor)
{
    if (id.isValid()) {
        SEMPLICE_ASSIGN(found, m_artists.get(id));
        if (!found)
            return Error::make(ErrorCode::NoArtist, QStringLiteral("artist %1 not found").arg(id.value));
        if (asAuthor)
            SEMPLICE_TRY(makeVisible(id));
        return id;
    }
    const QString display = Normalizer::display(name);
    if (display.isEmpty())
        return Error::make(ErrorCode::NoArtist, QStringLiteral("artist without id or name"));
    SEMPLICE_ASSIGN(existing, m_artists.findByName(display));
    if (existing) {
        if (asAuthor)
            SEMPLICE_TRY(makeVisible(existing->id));
        return existing->id;
    }
    Artist a;
    a.name = display;
    a.hidden = !asAuthor;
    a.createdAt = a.updatedAt = nowMs();
    if (asAuthor) {
        a.dirName = uniqueArtistDir(display);
        m_ctx->report.fileOps.append({FileOp::Kind::EnsureDir, kArtistsRoot + QLatin1Char('/') + a.dirName, {}});
    }
    SEMPLICE_ASSIGN(created, m_artists.insert(a));
    touch(*m_ctx, created);
    return created;
}

// Creates the track row for a draft (or finds the existing one) WITHOUT a container membership.
// `isNew` tells the caller whether the file still has to be placed.
Result<TrackId> LibraryService::resolveTrack(const TrackInput& input, bool defaultCreditPrimary, ArtistId primary,
                                             HomeRef home, bool& isNew)
{
    isNew = false;
    if (input.existing) {
        SEMPLICE_ASSIGN(found, m_tracks.get(*input.existing));
        if (!found)
            return Error::make(ErrorCode::NotFound, QStringLiteral("track %1").arg(input.existing->value));
        return found->id;
    }

    const TrackDraft& d = input.draft;
    SEMPLICE_ASSIGN(duplicate, m_tracks.findByHash(d.contentHash));
    if (duplicate) {
        addUnique(m_ctx->report.tracksReused, duplicate->id);
        return duplicate->id;  // same source bytes already in the library: reuse it
    }

    QList<CreditDraft> credits = d.credits;
    if (credits.isEmpty() && defaultCreditPrimary)
        credits.append(CreditDraft(primary));
    QList<TrackCredit> rows;
    int position = 0;
    for (const CreditDraft& c : std::as_const(credits)) {
        SEMPLICE_ASSIGN(artistId, findOrCreateArtist(c.artist, c.name, false));
        bool duplicateCredit = false;
        for (const TrackCredit& r : std::as_const(rows))
            duplicateCredit = duplicateCredit || r.artist == artistId;
        if (duplicateCredit)
            continue;
        rows.append({artistId, position++, c.role});
        m_ctx->creditTouched.insert(artistId);
        touch(*m_ctx, artistId);
    }
    if (rows.isEmpty())
        return Error::make(ErrorCode::NoArtist, QStringLiteral("track needs at least one artist"));

    const qint64 now = nowMs();
    Track t;
    t.title = Normalizer::display(d.title);
    t.durationMs = d.durationMs;
    t.filePath = QStringLiteral("\x01new/%1.%2").arg(QString::fromLatin1(d.contentHash.toHex()), d.extension);
    t.fileSize = d.fileSize;
    t.fileMtime = d.fileMtime;
    t.codec = d.codec;
    t.contentHash = d.contentHash;
    t.hasEmbeddedCover = d.hasEmbeddedCover;
    t.home = home;
    t.createdAt = t.updatedAt = now;
    SEMPLICE_ASSIGN(id, m_tracks.insert(t));

    SEMPLICE_TRY(m_tracks.setCredits(id, rows));
    SEMPLICE_TRY(m_tags.setTags(TagOwner::Track, id.value, d.tags));
    touch(*m_ctx, id);
    addUnique(m_ctx->report.tracksCreated, id);
    isNew = true;
    return id;
}

// Gives a freshly inserted track its real path and, if the draft names a source file, records the
// Copy (or, for a file already inside the library, the Move) that puts the audio there.
Result<void> LibraryService::placeNewTrack(TrackId id, const TrackDraft& draft)
{
    SEMPLICE_ASSIGN(track, m_tracks.get(id));
    if (!track)
        return Error::make(ErrorCode::NotFound, QStringLiteral("track"));
    SEMPLICE_ASSIGN(path, expectedTrackPath(*track, {}));
    SEMPLICE_TRY(m_tracks.setHome(id, track->home, path, nowMs()));
    m_ctx->dirty.insert(id);
    if (draft.sourcePath.isEmpty())
        return {};

    Context::NewFile nf;
    nf.id = id;
    nf.relativePath = path;
    if (draft.moveIntoLibrary) {
        const QString rel = QDir(m_env.libraryRoot).relativeFilePath(draft.sourcePath);
        m_ctx->report.fileOps.append({FileOp::Kind::MoveFile, rel, path});
    } else {
        m_ctx->report.fileOps.append({FileOp::Kind::Copy, draft.sourcePath, path});
        nf.source = draft.sourcePath;
        nf.expectedHash = draft.sourceHash.isEmpty() ? draft.contentHash : draft.sourceHash;
    }
    m_ctx->newFiles.append(nf);
    return {};
}

// Adds drafts / existing tracks to a container at positions position+1.. ; fills `position`.
Result<void> LibraryService::addNewTracks(ContainerKind kind, qint64 containerId, const QList<TrackInput>& tracks,
                                          ArtistId defaultCredit, int& position)
{
    const qint64 now = nowMs();
    for (const TrackInput& input : tracks) {
        bool isNew = false;
        SEMPLICE_ASSIGN(trackId, resolveTrack(input, defaultCredit.isValid(), defaultCredit, HomeRef{kind, containerId}, isNew));
        int already = 0;
        if (kind == ContainerKind::Album) {
            SEMPLICE_ASSIGN(p, m_albums.positionOf(AlbumId(containerId), trackId));
            already = p;
        } else {
            SEMPLICE_ASSIGN(p, m_playlists.positionOf(PlaylistId(containerId), trackId));
            already = p;
        }
        if (already > 0)
            continue;  // the same track listed twice
        if (kind == ContainerKind::Album)
            SEMPLICE_TRY(m_albums.addTrack(AlbumId(containerId), trackId, ++position, now));
        else
            SEMPLICE_TRY(m_playlists.addTrack(PlaylistId(containerId), trackId, ++position, now));
        if (isNew)
            SEMPLICE_TRY(placeNewTrack(trackId, input.draft));
        touch(*m_ctx, trackId);
    }
    return {};
}

Result<void> LibraryService::changeHome(TrackId id, HomeRef home)
{
    SEMPLICE_ASSIGN(track, m_tracks.get(id));
    if (!track)
        return Error::make(ErrorCode::NotFound, QStringLiteral("track"));
    SEMPLICE_TRY(m_tracks.setHome(id, home, track->filePath, nowMs()));
    SEMPLICE_TRY(syncTrackPaths({id}, true));
    addUnique(m_ctx->report.tracksMoved, id);
    touch(*m_ctx, id);
    return {};
}

// SPEC 4.2: the file moves to the membership with the earliest `added_at`, albums before playlists.
Result<void> LibraryService::moveHomeToNext(TrackId id)
{
    SEMPLICE_ASSIGN(memberships, m_tracks.memberships(id));
    if (memberships.isEmpty())
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("no membership left"));
    const Membership* chosen = nullptr;
    for (const Membership& m : std::as_const(memberships)) {
        if (m.kind == ContainerKind::Album) {
            chosen = &m;
            break;
        }
    }
    if (!chosen)
        chosen = &memberships.first();
    return changeHome(id, HomeRef{chosen->kind, chosen->containerId});
}

Result<void> LibraryService::markAlbumLost(const Album& album)
{
    m_ctx->albumLost.insert(album.primaryArtist);
    m_ctx->creditTouched.insert(album.primaryArtist);
    touch(*m_ctx, album.primaryArtist);
    SEMPLICE_ASSIGN(coAuthors, m_albums.coAuthors(album.id));
    for (const CoAuthor& c : std::as_const(coAuthors)) {
        m_ctx->albumLost.insert(c.artist);
        m_ctx->creditTouched.insert(c.artist);
        touch(*m_ctx, c.artist);
    }
    return {};
}

// ---------------------------------------------------------------------------------------------
// Deleting building blocks
// ---------------------------------------------------------------------------------------------

Result<void> LibraryService::eraseTrack(const Track& track)
{
    SEMPLICE_ASSIGN(credits, m_tracks.credits(track.id));
    for (const TrackCredit& c : std::as_const(credits)) {
        m_ctx->creditTouched.insert(c.artist);
        touch(*m_ctx, c.artist);
    }
    SEMPLICE_TRY(m_tracks.remove(track.id));
    m_ctx->report.fileOps.append({FileOp::Kind::Trash, track.filePath, {}});
    addUnique(m_ctx->report.tracksDeleted, track.id);
    touch(*m_ctx, track.id);
    return {};
}

// Removes the membership rows of a container that is going away and settles every track:
// tracks that live elsewhere stay (and their file moves if this was their home), others are erased.
Result<void> LibraryService::releaseTracks(ContainerKind kind, qint64 id, const QList<TrackId>& members)
{
    for (TrackId t : members) {
        if (kind == ContainerKind::Album)
            SEMPLICE_TRY(m_albums.removeTrack(AlbumId(id), t));
        else
            SEMPLICE_TRY(m_playlists.removeTrack(PlaylistId(id), t));

        SEMPLICE_ASSIGN(track, m_tracks.get(t));
        if (!track)
            continue;
        SEMPLICE_ASSIGN(left, m_tracks.membershipCount(t));
        if (left > 0) {
            addUnique(m_ctx->report.tracksKept, t);
            if (track->home == HomeRef{kind, id})
                SEMPLICE_TRY(moveHomeToNext(t));
        } else {
            SEMPLICE_TRY(eraseTrack(*track));
        }
    }
    return {};
}

Result<void> LibraryService::deleteAlbumInternal(AlbumId id)
{
    SEMPLICE_ASSIGN(album, m_albums.get(id));
    if (!album)
        return Error::make(ErrorCode::NotFound, QStringLiteral("album"));
    SEMPLICE_ASSIGN(members, m_albums.tracks(id));
    SEMPLICE_ASSIGN(folder, albumFolder(*album));

    SEMPLICE_TRY(markAlbumLost(*album));
    SEMPLICE_TRY(releaseTracks(ContainerKind::Album, id.value, members));
    SEMPLICE_TRY(m_recent.remove(ContainerKind::Album, id.value));
    SEMPLICE_TRY(m_albums.remove(id));
    m_ctx->report.fileOps.append({FileOp::Kind::Trash, folder, {}});
    addUnique(m_ctx->report.albumsDeleted, id);
    touch(*m_ctx, id);
    return {};
}

Result<void> LibraryService::deletePlaylistInternal(PlaylistId id)
{
    SEMPLICE_ASSIGN(playlist, m_playlists.get(id));
    if (!playlist)
        return Error::make(ErrorCode::NotFound, QStringLiteral("playlist"));
    SEMPLICE_ASSIGN(members, m_playlists.tracks(id));

    SEMPLICE_TRY(releaseTracks(ContainerKind::Playlist, id.value, members));
    SEMPLICE_TRY(m_recent.remove(ContainerKind::Playlist, id.value));
    SEMPLICE_TRY(m_playlists.remove(id));
    m_ctx->report.fileOps.append({FileOp::Kind::Trash, kPlaylistsRoot + QLatin1Char('/') + playlist->dirName, {}});
    addUnique(m_ctx->report.playlistsDeleted, id);
    touch(*m_ctx, id);
    return {};
}

// After a membership was removed from a container that stays alive.
Result<void> LibraryService::settleContainer(ContainerKind kind, qint64 id)
{
    if (kind == ContainerKind::Album) {
        SEMPLICE_ASSIGN(count, m_albums.trackCount(AlbumId(id)));
        if (count == 0)
            return deleteAlbumInternal(AlbumId(id));
        SEMPLICE_TRY(m_albums.renumber(AlbumId(id)));
        touch(*m_ctx, AlbumId(id));
    } else {
        SEMPLICE_ASSIGN(count, m_playlists.trackCount(PlaylistId(id)));
        if (count == 0)
            return deletePlaylistInternal(PlaylistId(id));
        SEMPLICE_TRY(m_playlists.renumber(PlaylistId(id)));
        touch(*m_ctx, PlaylistId(id));
    }
    return syncContainerPaths(kind, id, true);
}

Result<void> LibraryService::deleteTrackInternal(TrackId id)
{
    SEMPLICE_ASSIGN(track, m_tracks.get(id));
    if (!track)
        return Error::make(ErrorCode::NotFound, QStringLiteral("track"));
    SEMPLICE_ASSIGN(memberships, m_tracks.memberships(id));
    SEMPLICE_TRY(eraseTrack(*track));  // cascades the membership rows
    for (const Membership& m : std::as_const(memberships)) {
        // Rows are gone; renumber what is left and drop containers that became empty.
        SEMPLICE_TRY(settleContainer(m.kind, m.containerId));
    }
    return {};
}

// SPEC 4.3: the co-author with the lowest position becomes the primary artist; the folder moves.
Result<void> LibraryService::rehomePrimary(const Album& album)
{
    SEMPLICE_ASSIGN(coAuthors, m_albums.coAuthors(album.id));
    if (coAuthors.isEmpty())
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("album has no co-author"));
    const ArtistId oldPrimary = album.primaryArtist;
    const ArtistId newPrimary = coAuthors.first().artist;
    SEMPLICE_ASSIGN(oldFolder, albumFolder(album));
    SEMPLICE_TRY(makeVisible(newPrimary));

    // Same title already used by the new owner: suffix " (2)" on the title (and folder).
    QString title = album.title;
    for (int n = 2;; ++n) {
        SEMPLICE_ASSIGN(clash, m_albums.findByTitle(newPrimary, title));
        if (!clash)
            break;
        title = QStringLiteral("%1 (%2)").arg(album.title).arg(n);
    }
    const QString dir = uniqueAlbumDir(newPrimary, title);
    SEMPLICE_TRY(m_albums.setOwnership(album.id, newPrimary, title, dir, nowMs()));

    // The deleted artist stays on the album as the last co-author (it will be hidden, not erased).
    QList<CoAuthor> next;
    for (const CoAuthor& c : std::as_const(coAuthors)) {
        if (c.artist != newPrimary)
            next.append(c);
    }
    next.append({oldPrimary, static_cast<int>(next.size()) + 1});
    SEMPLICE_TRY(m_albums.setCoAuthors(album.id, next));

    SEMPLICE_ASSIGN(moved, m_albums.get(album.id));
    SEMPLICE_ASSIGN(newFolder, albumFolder(*moved));
    m_ctx->report.fileOps.append({FileOp::Kind::MoveDir, oldFolder, newFolder});
    SEMPLICE_TRY(syncContainerPaths(ContainerKind::Album, album.id.value, false));

    addUnique(m_ctx->report.albumsRehomed, album.id);
    touch(*m_ctx, album.id);
    touch(*m_ctx, oldPrimary);
    touch(*m_ctx, newPrimary);
    m_ctx->creditTouched.insert(oldPrimary);
    return {};
}

// Hidden artists nobody references are erased; a visible artist that lost its last real album
// becomes hidden if track credits remain (SPEC 5.1, 5.2).
Result<void> LibraryService::reevaluateArtists()
{
    QSet<ArtistId> done;
    for (;;) {
        QList<ArtistId> pending;
        for (ArtistId id : std::as_const(m_ctx->creditTouched))
            pending.append(id);
        for (ArtistId id : std::as_const(m_ctx->albumLost))
            addUnique(pending, id);
        pending.erase(std::remove_if(pending.begin(), pending.end(),
                                     [&](ArtistId id) { return done.contains(id) || m_ctx->skip.contains(id); }),
                      pending.end());
        if (pending.isEmpty())
            break;
        std::sort(pending.begin(), pending.end());

        for (ArtistId id : std::as_const(pending)) {
            done.insert(id);
            SEMPLICE_ASSIGN(artist, m_artists.get(id));
            if (!artist)
                continue;
            SEMPLICE_ASSIGN(credits, m_tracks.creditCount(id));
            SEMPLICE_ASSIGN(coAuthored, m_albums.coAuthorCount(id));

            if (artist->hidden) {
                if (credits == 0 && coAuthored == 0) {
                    SEMPLICE_TRY(m_artists.remove(id));
                    addUnique(m_ctx->report.artistsDeleted, id);
                    touch(*m_ctx, id);
                }
                continue;
            }
            if (!m_ctx->albumLost.contains(id))
                continue;

            SEMPLICE_ASSIGN(owned, m_albums.byPrimary(id));
            int realAlbums = coAuthored;
            for (const Album& a : std::as_const(owned)) {
                if (!a.isUnsorted)
                    ++realAlbums;
            }
            if (realAlbums > 0)
                continue;

            // Last real album gone: the Unsorted album (if any) dissolves, then hide when still credited.
            SEMPLICE_ASSIGN(unsorted, m_albums.unsortedOf(id));
            if (unsorted)
                SEMPLICE_TRY(deleteAlbumInternal(unsorted->id));
            SEMPLICE_ASSIGN(creditsLeft, m_tracks.creditCount(id));
            if (creditsLeft > 0) {
                SEMPLICE_ASSIGN(fresh, m_artists.get(id));
                if (fresh)
                    SEMPLICE_TRY(hideArtist(*fresh));
            }
        }
    }
    return {};
}

// ---------------------------------------------------------------------------------------------
// Public operations: creation
// ---------------------------------------------------------------------------------------------

// Copies an image in as <folder>/<owner>_cover.<ext> (replacing, via the Recycle Bin, the previous cover).
Result<QString> LibraryService::storeCover(const QString& folderRelative, const QString& ownerPrefix,
                                           const QString& sourceImage, const QString& oldCoverPath)
{
    const auto image = CoverDiscovery::loadImageFile(sourceImage);
    if (!image)
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("not a usable cover image: ") + sourceImage);
    const QString stored = folderRelative + QLatin1Char('/') + ownerPrefix + QStringLiteral("_cover.") + image->extension();
    if (!oldCoverPath.isEmpty())
        m_ctx->report.fileOps.append({FileOp::Kind::Trash, oldCoverPath, {}});
    m_ctx->report.fileOps.append({FileOp::Kind::Copy, sourceImage, stored});
    return stored;
}

void LibraryService::markDirty(TrackId id)
{
    m_ctx->dirty.insert(id);
}

Result<void> LibraryService::markDirtyForAlbum(AlbumId id)
{
    SEMPLICE_ASSIGN(members, m_albums.tracks(id));
    for (TrackId t : std::as_const(members)) {
        SEMPLICE_ASSIGN(track, m_tracks.get(t));
        if (track && track->home == HomeRef{ContainerKind::Album, id.value})
            markDirty(t);
    }
    return {};
}

Result<void> LibraryService::markDirtyForArtist(ArtistId id)
{
    SEMPLICE_ASSIGN(credited, m_tracks.creditedTo(id));
    for (TrackId t : std::as_const(credited))
        markDirty(t);
    // the album-artist field of every track homed in one of the artist's albums
    SEMPLICE_ASSIGN(owned, m_albums.byPrimary(id));
    for (const Album& a : std::as_const(owned))
        SEMPLICE_TRY(markDirtyForAlbum(a.id));
    SEMPLICE_ASSIGN(coAuthored, m_albums.byCoAuthor(id));
    for (const Album& a : std::as_const(coAuthored))
        SEMPLICE_TRY(markDirtyForAlbum(a.id));
    return {};
}

Result<ArtistId> LibraryService::createArtist(const ArtistDraft& draft)
{
    SEMPLICE_TRY(validateTitle(draft.name));
    SEMPLICE_TRY(validateTags(draft.tags));
    SEMPLICE_ASSIGN(existing, m_artists.findByName(draft.name));
    if (existing)
        return Error::make(ErrorCode::AlreadyExists, QStringLiteral("artist ") + draft.name);
    if (!draft.avatarPath.isEmpty() && !CoverDiscovery::loadImageFile(draft.avatarPath))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("not a usable image: ") + draft.avatarPath);

    ArtistId created;
    auto r = transact(false, [&]() -> Result<void> {
        Artist a;
        a.name = draft.name;
        a.hidden = draft.hidden;
        a.createdAt = a.updatedAt = nowMs();
        if (!a.hidden) {
            a.dirName = uniqueArtistDir(a.name);
            m_ctx->report.fileOps.append({FileOp::Kind::EnsureDir, kArtistsRoot + QLatin1Char('/') + a.dirName, {}});
        }
        SEMPLICE_ASSIGN(id, m_artists.insert(a));
        SEMPLICE_TRY(m_tags.setTags(TagOwner::Artist, id.value, draft.tags));
        if (!draft.avatarPath.isEmpty() && !a.hidden) {
            SEMPLICE_ASSIGN(stored, storeCover(kArtistsRoot + QLatin1Char('/') + a.dirName, QStringLiteral("artist"), draft.avatarPath, {}));
            SEMPLICE_TRY(m_artists.setAvatar(id, stored, nowMs()));
        }
        created = id;
        touch(*m_ctx, id);
        return {};
    });
    if (!r.ok())
        return r.error();
    return created;
}

Result<AlbumId> LibraryService::createAlbum(const AlbumDraft& draft, const JobContext& job)
{
    SEMPLICE_TRY(validateTitle(draft.title));
    if (Normalizer::identityKey(draft.title) == Normalizer::identityKey(kUnsortedDirName))
        return Error::make(ErrorCode::ReservedName, QStringLiteral("album title is reserved"));
    if (!draft.primaryArtist.isValid() && Normalizer::display(draft.primaryArtistName).isEmpty())
        return Error::make(ErrorCode::NoArtist);
    if (draft.tracks.isEmpty())
        return Error::make(ErrorCode::NoTracks);
    SEMPLICE_TRY(draft.date.validate(m_clock.now().date().year()));
    SEMPLICE_TRY(validateTags(draft.tags));
    SEMPLICE_TRY(validateTracks(draft.tracks, false));
    if (!draft.coverPath.isEmpty() && !CoverDiscovery::loadImageFile(draft.coverPath))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("not a usable cover image: ") + draft.coverPath);

    AlbumId created;
    auto r = transact(false, [&]() -> Result<void> {
        // An album's authors always have a profile: hidden artists become visible (UI doc p.38).
        SEMPLICE_ASSIGN(primaryId, findOrCreateArtist(draft.primaryArtist, draft.primaryArtistName, true));
        QList<ArtistId> authors{primaryId};
        QList<CoAuthor> coAuthors;
        auto addCo = [&](ArtistId id, const QString& name) -> Result<void> {
            SEMPLICE_ASSIGN(coId, findOrCreateArtist(id, name, true));
            if (authors.contains(coId))
                return Error::make(ErrorCode::InvalidArgument, QStringLiteral("duplicate album author"));
            authors.append(coId);
            coAuthors.append({coId, static_cast<int>(coAuthors.size()) + 1});
            return {};
        };
        for (ArtistId co : draft.coAuthors)
            SEMPLICE_TRY(addCo(co, {}));
        for (const QString& name : draft.coAuthorNames)
            SEMPLICE_TRY(addCo({}, name));

        SEMPLICE_ASSIGN(clash, m_albums.findByTitle(primaryId, draft.title));
        if (clash)
            return Error::make(ErrorCode::AlreadyExists, QStringLiteral("album ") + draft.title);

        const qint64 now = nowMs();
        Album album;
        album.primaryArtist = primaryId;
        album.title = draft.title;
        album.dirName = uniqueAlbumDir(primaryId, draft.title);
        album.type = draft.type;
        album.date = draft.date;
        album.createdAt = album.updatedAt = now;
        SEMPLICE_ASSIGN(id, m_albums.insert(album));
        album.id = id;
        created = id;

        SEMPLICE_TRY(m_albums.setCoAuthors(id, coAuthors));
        // an album given no tags of its own takes the tags of its first author
        QStringList albumTags = draft.tags;
        if (albumTags.isEmpty()) {
            SEMPLICE_ASSIGN(ownTags, m_tags.tags(TagOwner::Artist, primaryId.value));
            albumTags = ownTags;
        }
        SEMPLICE_TRY(m_tags.setTags(TagOwner::Album, id.value, albumTags));
        SEMPLICE_ASSIGN(folder, albumFolder(album));
        m_ctx->report.fileOps.append({FileOp::Kind::EnsureDir, folder, {}});

        int position = 0;
        SEMPLICE_TRY(addNewTracks(ContainerKind::Album, id.value, draft.tracks, primaryId, position));
        if (position == 0)
            return Error::make(ErrorCode::NoTracks);

        if (!draft.coverPath.isEmpty()) {
            SEMPLICE_ASSIGN(stored, storeCover(folder, QStringLiteral("album"), draft.coverPath, {}));
            SEMPLICE_TRY(m_albums.setCover(id, stored, now));
            SEMPLICE_TRY(markDirtyForAlbum(id));  // the front cover goes into the tracks, too
        }
        SEMPLICE_TRY(seedArtistTags(authors, draft.tags));
        touch(*m_ctx, id);
        for (ArtistId a : std::as_const(authors))
            touch(*m_ctx, a);
        return {};
    }, job);
    if (!r.ok())
        return r.error();
    return created;
}

Result<PlaylistId> LibraryService::createPlaylist(const PlaylistDraft& draft, const JobContext& job)
{
    SEMPLICE_TRY(validateTitle(draft.name));
    if (draft.tracks.isEmpty())
        return Error::make(ErrorCode::NoTracks);
    SEMPLICE_TRY(validateTracks(draft.tracks, true));
    SEMPLICE_ASSIGN(clash, m_playlists.findByName(draft.name));
    if (clash)
        return Error::make(ErrorCode::AlreadyExists, QStringLiteral("playlist ") + draft.name);
    if (!draft.coverPath.isEmpty() && !CoverDiscovery::loadImageFile(draft.coverPath))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("not a usable cover image: ") + draft.coverPath);

    PlaylistId created;
    auto r = transact(false, [&]() -> Result<void> {
        const qint64 now = nowMs();
        Playlist p;
        p.name = draft.name;
        p.dirName = uniquePlaylistDir(draft.name);
        p.createdAt = p.updatedAt = now;
        SEMPLICE_ASSIGN(id, m_playlists.insert(p));
        created = id;
        const QString folder = kPlaylistsRoot + QLatin1Char('/') + p.dirName;
        m_ctx->report.fileOps.append({FileOp::Kind::EnsureDir, folder, {}});

        int position = 0;
        SEMPLICE_TRY(addNewTracks(ContainerKind::Playlist, id.value, draft.tracks, {}, position));
        if (position == 0)
            return Error::make(ErrorCode::NoTracks);
        if (!draft.coverPath.isEmpty()) {
            SEMPLICE_ASSIGN(stored, storeCover(folder, QStringLiteral("playlist"), draft.coverPath, {}));
            SEMPLICE_TRY(m_playlists.setCover(id, stored, now));
        }
        touch(*m_ctx, id);
        return {};
    }, job);
    if (!r.ok())
        return r.error();
    return created;
}

Result<void> LibraryService::appendTracksToAlbum(AlbumId albumId, const QList<TrackInput>& tracks, const JobContext& job)
{
    if (tracks.isEmpty())
        return Error::make(ErrorCode::NoTracks);
    SEMPLICE_TRY(validateTracks(tracks, false));
    SEMPLICE_ASSIGN(album, m_albums.get(albumId));
    if (!album)
        return Error::make(ErrorCode::NotFound, QStringLiteral("album"));
    if (album->isUnsorted)
        return Error::make(ErrorCode::NotAllowed, QStringLiteral("use addToDiscography for the Unsorted album"));
    auto r = transact(false, [&]() -> Result<void> {
        SEMPLICE_ASSIGN(position, m_albums.trackCount(albumId));
        SEMPLICE_TRY(addNewTracks(ContainerKind::Album, albumId.value, tracks, album->primaryArtist, position));
        touch(*m_ctx, albumId);
        return {};
    }, job);
    if (!r.ok())
        return r.error();
    return {};
}

Result<void> LibraryService::appendTracksToPlaylist(PlaylistId playlistId, const QList<TrackInput>& tracks, const JobContext& job)
{
    if (tracks.isEmpty())
        return Error::make(ErrorCode::NoTracks);
    SEMPLICE_TRY(validateTracks(tracks, true));
    SEMPLICE_ASSIGN(playlist, m_playlists.get(playlistId));
    if (!playlist)
        return Error::make(ErrorCode::NotFound, QStringLiteral("playlist"));
    auto r = transact(false, [&]() -> Result<void> {
        SEMPLICE_ASSIGN(position, m_playlists.trackCount(playlistId));
        SEMPLICE_TRY(addNewTracks(ContainerKind::Playlist, playlistId.value, tracks, {}, position));
        touch(*m_ctx, playlistId);
        return {};
    }, job);
    if (!r.ok())
        return r.error();
    return {};
}

// ---------------------------------------------------------------------------------------------
// Public operations: memberships
// ---------------------------------------------------------------------------------------------

Result<void> LibraryService::addTrackToAlbum(TrackId track, AlbumId albumId)
{
    SEMPLICE_ASSIGN(t, m_tracks.get(track));
    SEMPLICE_ASSIGN(album, m_albums.get(albumId));
    if (!t || !album)
        return Error::make(ErrorCode::NotFound);
    if (album->isUnsorted)
        return Error::make(ErrorCode::NotAllowed, QStringLiteral("use addToDiscography for the Unsorted album"));
    SEMPLICE_ASSIGN(existing, m_albums.positionOf(albumId, track));
    if (existing > 0)
        return {};

    auto r = transact(false, [&]() -> Result<void> {
        SEMPLICE_ASSIGN(count, m_albums.trackCount(albumId));
        SEMPLICE_TRY(m_albums.addTrack(albumId, track, count + 1, nowMs()));
        touch(*m_ctx, albumId);
        touch(*m_ctx, track);
        return {};
    });
    if (!r.ok())
        return r.error();
    return {};
}

Result<void> LibraryService::addTrackToPlaylist(TrackId track, PlaylistId playlistId)
{
    SEMPLICE_ASSIGN(t, m_tracks.get(track));
    SEMPLICE_ASSIGN(playlist, m_playlists.get(playlistId));
    if (!t || !playlist)
        return Error::make(ErrorCode::NotFound);
    SEMPLICE_ASSIGN(existing, m_playlists.positionOf(playlistId, track));
    if (existing > 0)
        return {};

    auto r = transact(false, [&]() -> Result<void> {
        SEMPLICE_ASSIGN(count, m_playlists.trackCount(playlistId));
        SEMPLICE_TRY(m_playlists.addTrack(playlistId, track, count + 1, nowMs()));
        touch(*m_ctx, playlistId);
        touch(*m_ctx, track);
        return {};
    });
    if (!r.ok())
        return r.error();
    return {};
}

Result<void> LibraryService::removeTrackFromAlbum(TrackId track, AlbumId albumId)
{
    SEMPLICE_ASSIGN(position, m_albums.positionOf(albumId, track));
    if (position == 0)
        return Error::make(ErrorCode::NotFound, QStringLiteral("track is not in this album"));
    SEMPLICE_ASSIGN(memberCount, m_tracks.membershipCount(track));
    if (memberCount < 2)
        return Error::make(ErrorCode::NotAllowed, QStringLiteral("track exists nowhere else"));

    auto r = transact(false, [&]() -> Result<void> {
        SEMPLICE_ASSIGN(t, m_tracks.get(track));
        SEMPLICE_TRY(m_albums.removeTrack(albumId, track));
        addUnique(m_ctx->report.tracksKept, track);
        if (t && t->home == HomeRef{ContainerKind::Album, albumId.value})
            SEMPLICE_TRY(moveHomeToNext(track));
        touch(*m_ctx, track);
        return settleContainer(ContainerKind::Album, albumId.value);
    });
    if (!r.ok())
        return r.error();
    return {};
}

Result<void> LibraryService::removeTrackFromPlaylist(TrackId track, PlaylistId playlistId)
{
    SEMPLICE_ASSIGN(position, m_playlists.positionOf(playlistId, track));
    if (position == 0)
        return Error::make(ErrorCode::NotFound, QStringLiteral("track is not in this playlist"));
    SEMPLICE_ASSIGN(memberCount, m_tracks.membershipCount(track));
    if (memberCount < 2)
        return Error::make(ErrorCode::NotAllowed, QStringLiteral("track exists nowhere else"));

    auto r = transact(false, [&]() -> Result<void> {
        SEMPLICE_ASSIGN(t, m_tracks.get(track));
        SEMPLICE_TRY(m_playlists.removeTrack(playlistId, track));
        addUnique(m_ctx->report.tracksKept, track);
        if (t && t->home == HomeRef{ContainerKind::Playlist, playlistId.value})
            SEMPLICE_TRY(moveHomeToNext(track));
        touch(*m_ctx, track);
        return settleContainer(ContainerKind::Playlist, playlistId.value);
    });
    if (!r.ok())
        return r.error();
    return {};
}

// ---------------------------------------------------------------------------------------------
// Unsorted tracks
// ---------------------------------------------------------------------------------------------

Result<void> LibraryService::addToDiscographyInternal(TrackId track, const Album& unsorted, const Artist& artist)
{
    SEMPLICE_ASSIGN(credits, m_tracks.credits(track));
    const bool credited = std::any_of(credits.begin(), credits.end(),
                                      [&](const TrackCredit& c) { return c.artist == artist.id; });
    if (!credited) {
        SEMPLICE_TRY(m_tracks.addCredit(track, {artist.id, 0, CreditRole::Main}));
        m_ctx->creditTouched.insert(artist.id);
    }
    SEMPLICE_ASSIGN(existing, m_albums.positionOf(unsorted.id, track));
    if (existing == 0) {
        SEMPLICE_ASSIGN(count, m_albums.trackCount(unsorted.id));
        SEMPLICE_TRY(m_albums.addTrack(unsorted.id, track, count + 1, nowMs()));
    }
    SEMPLICE_ASSIGN(t, m_tracks.get(track));
    if (t && t->home.kind == ContainerKind::Playlist)
        SEMPLICE_TRY(changeHome(track, HomeRef{ContainerKind::Album, unsorted.id.value}));
    touch(*m_ctx, track);
    touch(*m_ctx, unsorted.id);
    touch(*m_ctx, artist.id);
    return {};
}

Result<AlbumId> LibraryService::addToDiscography(TrackId track, ArtistId artistId)
{
    SEMPLICE_ASSIGN(t, m_tracks.get(track));
    SEMPLICE_ASSIGN(artist, m_artists.get(artistId));
    if (!t || !artist)
        return Error::make(ErrorCode::NotFound);

    AlbumId result;
    auto r = transact(false, [&]() -> Result<void> {
        SEMPLICE_ASSIGN(unsorted, ensureUnsortedAlbum(artistId));
        SEMPLICE_ASSIGN(fresh, m_artists.get(artistId));
        SEMPLICE_TRY(addToDiscographyInternal(track, unsorted, *fresh));
        result = unsorted.id;
        return {};
    });
    if (!r.ok())
        return r.error();
    return result;
}

Result<AlbumId> LibraryService::materialiseHiddenArtist(ArtistId artistId)
{
    SEMPLICE_ASSIGN(artist, m_artists.get(artistId));
    if (!artist)
        return Error::make(ErrorCode::NotFound);
    SEMPLICE_ASSIGN(virtualTracks, m_tracks.unsortedOfHiddenArtist(artistId));
    if (virtualTracks.isEmpty()) {
        SEMPLICE_ASSIGN(existing, m_albums.unsortedOf(artistId));
        if (existing)
            return existing->id;
        return Error::make(ErrorCode::NoTracks, QStringLiteral("artist has no tracks to materialise"));
    }

    AlbumId result;
    auto r = transact(false, [&]() -> Result<void> {
        SEMPLICE_ASSIGN(unsorted, ensureUnsortedAlbum(artistId));
        SEMPLICE_ASSIGN(fresh, m_artists.get(artistId));
        for (TrackId t : std::as_const(virtualTracks))
            SEMPLICE_TRY(addToDiscographyInternal(t, unsorted, *fresh));
        result = unsorted.id;
        return {};
    });
    if (!r.ok())
        return r.error();
    return result;
}

// ---------------------------------------------------------------------------------------------
// Public operations: deletion
// ---------------------------------------------------------------------------------------------

Result<OperationReport> LibraryService::doDeleteTrack(TrackId track, bool dry)
{
    SEMPLICE_ASSIGN(t, m_tracks.get(track));
    if (!t)
        return Error::make(ErrorCode::NotFound);
    return transact(dry, [&]() { return deleteTrackInternal(track); });
}

Result<OperationReport> LibraryService::doDeleteAlbum(AlbumId album, bool dry)
{
    SEMPLICE_ASSIGN(a, m_albums.get(album));
    if (!a)
        return Error::make(ErrorCode::NotFound);
    return transact(dry, [&]() { return deleteAlbumInternal(album); });
}

Result<OperationReport> LibraryService::doDeletePlaylist(PlaylistId playlist, bool dry)
{
    SEMPLICE_ASSIGN(p, m_playlists.get(playlist));
    if (!p)
        return Error::make(ErrorCode::NotFound);
    return transact(dry, [&]() { return deletePlaylistInternal(playlist); });
}

// SPEC 6.4 / UI doc p.33: albums without dependencies go; collaborative albums are re-homed;
// the artist is erased when nothing references it any more, otherwise kept hidden.
Result<OperationReport> LibraryService::doDeleteArtist(ArtistId id, bool dry)
{
    SEMPLICE_ASSIGN(artist, m_artists.get(id));
    if (!artist)
        return Error::make(ErrorCode::NotFound);

    return transact(dry, [&]() -> Result<void> {
        m_ctx->skip.insert(id);
        SEMPLICE_ASSIGN(owned, m_albums.byPrimary(id));
        for (const Album& a : std::as_const(owned)) {
            if (a.isUnsorted)
                continue;
            SEMPLICE_ASSIGN(coAuthors, m_albums.coAuthors(a.id));
            if (coAuthors.isEmpty())
                SEMPLICE_TRY(deleteAlbumInternal(a.id));
            else
                SEMPLICE_TRY(rehomePrimary(a));
        }
        SEMPLICE_ASSIGN(unsorted, m_albums.unsortedOf(id));
        if (unsorted)
            SEMPLICE_TRY(deleteAlbumInternal(unsorted->id));

        SEMPLICE_ASSIGN(credits, m_tracks.creditCount(id));
        SEMPLICE_ASSIGN(coAuthored, m_albums.coAuthorCount(id));
        SEMPLICE_ASSIGN(fresh, m_artists.get(id));
        if (credits + coAuthored > 0) {
            if (!fresh->hidden)
                SEMPLICE_TRY(hideArtist(*fresh));
        } else {
            if (!fresh->dirName.isEmpty())
                m_ctx->report.fileOps.append(
                    {FileOp::Kind::Trash, kArtistsRoot + QLatin1Char('/') + fresh->dirName, {}});
            SEMPLICE_TRY(m_artists.remove(id));
            addUnique(m_ctx->report.artistsDeleted, id);
        }
        touch(*m_ctx, id);
        return {};
    });
}

Result<DeletePlan> LibraryService::planDeleteTrack(TrackId t) { return doDeleteTrack(t, true); }
Result<OperationReport> LibraryService::deleteTrack(TrackId t) { return doDeleteTrack(t, false); }
Result<DeletePlan> LibraryService::planDeleteAlbum(AlbumId a) { return doDeleteAlbum(a, true); }
Result<OperationReport> LibraryService::deleteAlbum(AlbumId a) { return doDeleteAlbum(a, false); }
Result<DeletePlan> LibraryService::planDeletePlaylist(PlaylistId p) { return doDeletePlaylist(p, true); }
Result<OperationReport> LibraryService::deletePlaylist(PlaylistId p) { return doDeletePlaylist(p, false); }
Result<DeletePlan> LibraryService::planDeleteArtist(ArtistId a) { return doDeleteArtist(a, true); }
Result<OperationReport> LibraryService::deleteArtist(ArtistId a) { return doDeleteArtist(a, false); }

}  // namespace semplice
