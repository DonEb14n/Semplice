// LibraryService: editing operations (SPEC 4.3 "folders follow names") and the tag sync (SPEC 6.2).

#include "core/services/LibraryService.h"

#include "core/services/LibraryServiceInternal.h"
#include "core/tags/CoverDiscovery.h"
#include "core/util/Logger.h"
#include "core/util/Normalizer.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace semplice {

using namespace internal;

// ---------------------------------------------------------------------------------------------
// Tag sync
// ---------------------------------------------------------------------------------------------

Result<TagWrite> LibraryService::buildTagWrite(TrackId id)
{
    SEMPLICE_ASSIGN(track, m_tracks.get(id));
    if (!track)
        return Error::make(ErrorCode::NotFound, QStringLiteral("track"));

    TagWrite w;
    w.title = track->title;
    SEMPLICE_ASSIGN(credits, m_tracks.credits(id));
    for (const TrackCredit& c : std::as_const(credits)) {
        SEMPLICE_ASSIGN(artist, m_artists.get(c.artist));
        if (artist)
            w.artists << artist->name;
    }

    // Effective genres (SPEC 5.4): the track's own tags plus the tags of every album that contains it.
    QStringList keys;
    SEMPLICE_ASSIGN(own, m_tags.tags(TagOwner::Track, id.value));
    keys += own;
    SEMPLICE_ASSIGN(memberships, m_tracks.memberships(id));
    for (const Membership& m : std::as_const(memberships)) {
        if (m.kind == ContainerKind::Album) {
            SEMPLICE_ASSIGN(albumTags, m_tags.tags(TagOwner::Album, m.containerId));
            keys += albumTags;
        }
    }
    keys.removeDuplicates();
    keys.sort();
    for (const QString& key : std::as_const(keys)) {
        if (!GenreVocabulary::isGenreKey(key))
            continue;  // language and mood tags are not genres: they stay in the database
        const QString name = m_env.genres ? m_env.genres->name(key, QStringLiteral("en")) : key;  // English on disk
        if (!w.genres.contains(name, Qt::CaseInsensitive))  // two keys may share a name; a file cannot hold it twice
            w.genres << name;
    }

    if (track->home.kind == ContainerKind::Album) {
        SEMPLICE_ASSIGN(album, m_albums.get(AlbumId(track->home.id)));
        // The Unsorted album has no title and no tags: like a playlist, it leaves the album fields alone.
        if (album && !album->isUnsorted) {
            w.album = album->title;
            QStringList authors;
            SEMPLICE_ASSIGN(primary, m_artists.get(album->primaryArtist));
            if (primary)
                authors << primary->name;
            SEMPLICE_ASSIGN(coAuthors, m_albums.coAuthors(album->id));
            for (const CoAuthor& c : std::as_const(coAuthors)) {
                SEMPLICE_ASSIGN(co, m_artists.get(c.artist));
                if (co)
                    authors << co->name;
            }
            w.albumArtists = authors;
            SEMPLICE_ASSIGN(position, m_albums.positionOf(album->id, id));
            SEMPLICE_ASSIGN(total, m_albums.trackCount(album->id));
            w.trackNumber = position;
            w.trackTotal = total;
            w.date = album->date;
            if (!album->coverPath.isEmpty()) {
                if (const auto image = CoverDiscovery::loadImageFile(absolute(album->coverPath))) {
                    w.coverAction = CoverAction::Set;
                    w.cover = *image;
                }
            }
        }
    }
    return w;
}

Result<void> LibraryService::writeTagsOf(TrackId id, bool newFile)
{
    if (!m_env.tags)
        return {};
    SEMPLICE_ASSIGN(track, m_tracks.get(id));
    if (!track)
        return Error::make(ErrorCode::NotFound, QStringLiteral("track"));
    SEMPLICE_ASSIGN(write, buildTagWrite(id));
    const QString path = absolute(track->filePath);
    if (!QFileInfo(path).isFile())
        return Error::make(ErrorCode::NotFound, QStringLiteral("file is missing: ") + track->filePath);
    SEMPLICE_TRY(newFile ? m_env.tags->writeNewFile(path, write) : m_env.tags->write(path, write));
    if (!newFile) {
        const QFileInfo info(path);
        SEMPLICE_TRY(m_tracks.setFileInfo(id, info.size(), info.lastModified().toMSecsSinceEpoch(), nowMs()));
        SEMPLICE_TRY(m_tracks.setTagsDirty(id, false));
    }
    return {};
}

Result<QList<TrackId>> LibraryService::syncDirtyTags(const JobContext& job, QSet<qint64>* giveUp)
{
    QList<TrackId> failed;
    if (!m_env.tags)
        return failed;
    SEMPLICE_ASSIGN(dirty, m_tracks.dirtyTracks());
    qint64 done = 0;
    for (TrackId id : std::as_const(dirty)) {
        if (job.cancelled())
            return Error::make(ErrorCode::Cancelled);
        if (giveUp && giveUp->contains(id.value))
            continue;
        job.report(QStringLiteral("tags"), done++, dirty.size());
        if (auto r = writeTagsOf(id, false); !r.ok()) {
            failed << id;
            if (giveUp && r.error().code != ErrorCode::FileLocked)
                giveUp->insert(id.value);
            SLOG_WARN(QStringLiteral("tags still out of sync for track %1: %2").arg(id.value).arg(r.error().devMessage));
        }
    }
    job.report(QStringLiteral("tags"), dirty.size(), dirty.size());
    return failed;
}

// ---------------------------------------------------------------------------------------------
// Folder renames and moves
// ---------------------------------------------------------------------------------------------

// Cover paths contain folder names: after any folder change they are recomputed from the album's folder.
Result<void> LibraryService::relocateCovers(const Album& stale)
{
    SEMPLICE_ASSIGN(album, m_albums.get(stale.id));
    if (!album || album->coverPath.isEmpty())
        return {};
    SEMPLICE_ASSIGN(folder, albumFolder(*album));
    const QString fresh = folder + QLatin1Char('/') + QFileInfo(album->coverPath).fileName();
    if (fresh != album->coverPath)
        SEMPLICE_TRY(m_albums.setCover(album->id, fresh, nowMs()));
    return {};
}

// Moves/renames the album folder (new title and/or new primary artist) and fixes every path inside it.
Result<void> LibraryService::renameAlbumFolder(const Album& album, ArtistId newPrimary, const QString& newTitle)
{
    SEMPLICE_ASSIGN(oldFolder, albumFolder(album));
    const QString dir = uniqueAlbumDir(newPrimary, newTitle, newPrimary == album.primaryArtist ? album.id : AlbumId());
    SEMPLICE_TRY(m_albums.setOwnership(album.id, newPrimary, newTitle, dir, nowMs()));
    SEMPLICE_ASSIGN(moved, m_albums.get(album.id));
    SEMPLICE_ASSIGN(newFolder, albumFolder(*moved));
    if (newFolder != oldFolder)
        m_ctx->report.fileOps.append({FileOp::Kind::MoveDir, oldFolder, newFolder});
    SEMPLICE_TRY(syncContainerPaths(ContainerKind::Album, album.id.value, false));
    return relocateCovers(*moved);
}

// ---------------------------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------------------------

Result<OperationReport> LibraryService::updateArtist(ArtistId id, const ArtistPatch& patch, const JobContext& job)
{
    SEMPLICE_ASSIGN(artist, m_artists.get(id));
    if (!artist)
        return Error::make(ErrorCode::NotFound, QStringLiteral("artist"));
    if (patch.name) {
        SEMPLICE_TRY(validateTitle(*patch.name));
        SEMPLICE_ASSIGN(clash, m_artists.findByName(*patch.name));
        if (clash && clash->id != id) {
            // the name is taken: the two artists become one (the one being renamed has priority)
            SEMPLICE_TRY(mergeArtistInto(clash->id, id, job));
            SEMPLICE_ASSIGN(refreshed, m_artists.get(id));  // a hidden artist that gained albums is visible now
            if (!refreshed)
                return Error::make(ErrorCode::NotFound, QStringLiteral("artist"));
            artist = refreshed;
        }
    }
    if (patch.tags)
        SEMPLICE_TRY(validateTags(*patch.tags));
    if (patch.avatarPath && !patch.avatarPath->isEmpty() && !CoverDiscovery::loadImageFile(*patch.avatarPath))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("not a usable image: ") + *patch.avatarPath);

    return transact(false, [&]() -> Result<void> {
        QString dir = artist->dirName;
        if (patch.name && Normalizer::display(*patch.name) != artist->name) {
            const QString newName = Normalizer::display(*patch.name);
            const QString oldDir = artist->dirName;
            if (!artist->hidden)
                dir = uniqueArtistDir(newName, id);
            SEMPLICE_TRY(m_artists.setName(id, newName, artist->hidden ? QString() : dir, nowMs()));
            if (!artist->hidden && dir != oldDir)
                m_ctx->report.fileOps.append({FileOp::Kind::MoveDir, kArtistsRoot + QLatin1Char('/') + oldDir,
                                              kArtistsRoot + QLatin1Char('/') + dir});
            // every file below the artist's folder: new path (the MoveDir covers the disk side)
            SEMPLICE_ASSIGN(owned, m_albums.byPrimary(id));
            for (const Album& a : std::as_const(owned)) {
                SEMPLICE_TRY(syncContainerPaths(ContainerKind::Album, a.id.value, false));
                SEMPLICE_TRY(relocateCovers(a));
                touch(*m_ctx, a.id);
            }
            if (!artist->avatarPath.isEmpty() && !artist->hidden) {
                SEMPLICE_TRY(m_artists.setAvatar(id, kArtistsRoot + QLatin1Char('/') + dir + QLatin1Char('/') +
                                                         QFileInfo(artist->avatarPath).fileName(),
                                                 nowMs()));
            }
            // playlist files are named "Title - Artist.ext": the new name changes them (a real file move)
            SEMPLICE_ASSIGN(credited, m_tracks.creditedTo(id));
            SEMPLICE_TRY(syncTrackPaths(credited, true));
            SEMPLICE_TRY(markDirtyForArtist(id));
        }
        if (patch.tags) {
            SEMPLICE_TRY(m_tags.setTags(TagOwner::Artist, id.value, *patch.tags));
            SEMPLICE_TRY(m_artists.setTagsSeeded(id, !patch.tags->isEmpty() || artist->tagsSeeded));
        }
        if (patch.userHidden) {
            SEMPLICE_TRY(m_artists.setUserHidden(id, *patch.userHidden, nowMs()));
            touch(*m_ctx, id);
        }
        if (patch.avatarPath) {
            SEMPLICE_ASSIGN(freshRow, m_artists.get(id));
            if (!freshRow)
                return Error::make(ErrorCode::NotFound, QStringLiteral("artist vanished"));
            const Artist fresh = *freshRow;
            const QString old = fresh.avatarPath;
            if (fresh.hidden)
                return Error::make(ErrorCode::NotAllowed, QStringLiteral("a hidden artist has no folder for an avatar"));
            if (patch.avatarPath->isEmpty()) {
                if (!old.isEmpty())
                    m_ctx->report.fileOps.append({FileOp::Kind::Trash, old, {}});
                SEMPLICE_TRY(m_artists.setAvatar(id, QString(), nowMs()));
            } else {
                SEMPLICE_ASSIGN(stored, storeCover(kArtistsRoot + QLatin1Char('/') + fresh.dirName, QStringLiteral("artist"),
                                                   *patch.avatarPath, old));
                SEMPLICE_TRY(m_artists.setAvatar(id, stored, nowMs()));
            }
        }
        touch(*m_ctx, id);
        return {};
    }, job);
}

Result<OperationReport> LibraryService::updateAlbum(AlbumId id, const AlbumPatch& patch, const JobContext& job)
{
    SEMPLICE_ASSIGN(album, m_albums.get(id));
    if (!album)
        return Error::make(ErrorCode::NotFound, QStringLiteral("album"));
    const bool structural = patch.title || patch.type || patch.date || patch.tags || patch.coverPath || patch.primaryArtist ||
                            patch.coAuthors;
    if (album->isUnsorted && structural)
        return Error::make(ErrorCode::NotAllowed, QStringLiteral("the Unsorted album cannot be edited"));
    if (patch.title) {
        SEMPLICE_TRY(validateTitle(*patch.title));
        if (Normalizer::identityKey(*patch.title) == Normalizer::identityKey(kUnsortedDirName))
            return Error::make(ErrorCode::ReservedName, QStringLiteral("album title is reserved"));
    }
    if (patch.date)
        SEMPLICE_TRY(patch.date->validate(m_clock.now().date().year()));
    if (patch.tags)
        SEMPLICE_TRY(validateTags(*patch.tags));
    if (patch.coverPath && !patch.coverPath->isEmpty() && !CoverDiscovery::loadImageFile(*patch.coverPath))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("not a usable cover image: ") + *patch.coverPath);

    return transact(false, [&]() -> Result<void> {
        const ArtistId oldPrimary = album->primaryArtist;
        ArtistId newPrimary = oldPrimary;
        if (patch.primaryArtist) {
            SEMPLICE_ASSIGN(p, findOrCreateArtist(*patch.primaryArtist, {}, true));
            newPrimary = p;
        }
        const QString newTitle = patch.title ? Normalizer::display(*patch.title) : album->title;

        // 1. name and/or owner
        if (newPrimary != oldPrimary || newTitle != album->title) {
            SEMPLICE_ASSIGN(clash, m_albums.findByTitle(newPrimary, newTitle));
            if (clash && clash->id != id)
                return Error::make(ErrorCode::AlreadyExists, QStringLiteral("album ") + newTitle);
            SEMPLICE_TRY(renameAlbumFolder(*album, newPrimary, newTitle));
            if (newPrimary != oldPrimary) {
                m_ctx->albumLost.insert(oldPrimary);  // the previous owner may become hidden / be cleaned up
                m_ctx->creditTouched.insert(oldPrimary);
                touch(*m_ctx, oldPrimary);
                touch(*m_ctx, newPrimary);
            }
            SEMPLICE_TRY(markDirtyForAlbum(id));
        }
        // 2. type and date
        if (patch.type || patch.date) {
            SEMPLICE_TRY(m_albums.setMeta(id, patch.type.value_or(album->type), patch.date.value_or(album->date), nowMs()));
            if (patch.date)
                SEMPLICE_TRY(markDirtyForAlbum(id));
        }
        // 3. co-authors
        if (patch.coAuthors) {
            SEMPLICE_ASSIGN(before, m_albums.coAuthors(id));
            QList<CoAuthor> next;
            for (ArtistId candidate : *patch.coAuthors) {
                SEMPLICE_ASSIGN(coId, findOrCreateArtist(candidate, {}, true));
                if (coId == newPrimary || std::any_of(next.begin(), next.end(), [&](const CoAuthor& c) { return c.artist == coId; }))
                    return Error::make(ErrorCode::InvalidArgument, QStringLiteral("duplicate album author"));
                next.append({coId, static_cast<int>(next.size()) + 1});
            }
            SEMPLICE_TRY(m_albums.setCoAuthors(id, next));
            for (const CoAuthor& old : std::as_const(before)) {
                const bool kept = std::any_of(next.begin(), next.end(), [&](const CoAuthor& c) { return c.artist == old.artist; });
                if (!kept) {
                    m_ctx->albumLost.insert(old.artist);
                    m_ctx->creditTouched.insert(old.artist);
                    touch(*m_ctx, old.artist);
                }
            }
            SEMPLICE_TRY(markDirtyForAlbum(id));
        }
        // 4. tags (the one-time seeding of authors applies to authors that have none yet)
        if (patch.tags) {
            SEMPLICE_TRY(m_tags.setTags(TagOwner::Album, id.value, *patch.tags));
            SEMPLICE_ASSIGN(fresh, m_albums.get(id));
            QList<ArtistId> authors{fresh->primaryArtist};
            SEMPLICE_ASSIGN(co, m_albums.coAuthors(id));
            for (const CoAuthor& c : std::as_const(co))
                authors << c.artist;
            SEMPLICE_TRY(seedArtistTags(authors, *patch.tags));
            SEMPLICE_TRY(markDirtyForAlbum(id));
        }
        // 5. cover
        if (patch.coverPath) {
            SEMPLICE_ASSIGN(fresh, m_albums.get(id));
            SEMPLICE_ASSIGN(folder, albumFolder(*fresh));
            if (patch.coverPath->isEmpty()) {
                if (!fresh->coverPath.isEmpty())
                    m_ctx->report.fileOps.append({FileOp::Kind::Trash, fresh->coverPath, {}});
                SEMPLICE_TRY(m_albums.setCover(id, QString(), nowMs()));
            } else {
                SEMPLICE_ASSIGN(stored, storeCover(folder, QStringLiteral("album"), *patch.coverPath, fresh->coverPath));
                SEMPLICE_TRY(m_albums.setCover(id, stored, nowMs()));
            }
            SEMPLICE_TRY(markDirtyForAlbum(id));
        }
        // 6. order of the tracks
        if (patch.trackOrder) {
            SEMPLICE_ASSIGN(current, m_albums.tracks(id));
            QList<TrackId> a = current;
            QList<TrackId> b = *patch.trackOrder;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            if (a != b)
                return Error::make(ErrorCode::InvalidArgument, QStringLiteral("track order must list exactly the album's tracks"));
            SEMPLICE_TRY(m_albums.reorder(id, *patch.trackOrder));
            SEMPLICE_TRY(syncContainerPaths(ContainerKind::Album, id.value, true));
            SEMPLICE_TRY(markDirtyForAlbum(id));
        }
        touch(*m_ctx, id);
        return {};
    }, job);
}

Result<OperationReport> LibraryService::updatePlaylist(PlaylistId id, const PlaylistPatch& patch, const JobContext& job)
{
    SEMPLICE_ASSIGN(playlist, m_playlists.get(id));
    if (!playlist)
        return Error::make(ErrorCode::NotFound, QStringLiteral("playlist"));
    if (patch.name) {
        SEMPLICE_TRY(validateTitle(*patch.name));
        SEMPLICE_ASSIGN(clash, m_playlists.findByName(*patch.name));
        if (clash && clash->id != id)
            return Error::make(ErrorCode::AlreadyExists, QStringLiteral("playlist ") + *patch.name);
    }
    if (patch.coverPath && !patch.coverPath->isEmpty() && !CoverDiscovery::loadImageFile(*patch.coverPath))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("not a usable cover image: ") + *patch.coverPath);

    return transact(false, [&]() -> Result<void> {
        QString dir = playlist->dirName;
        if (patch.name && Normalizer::display(*patch.name) != playlist->name) {
            const QString newName = Normalizer::display(*patch.name);
            dir = uniquePlaylistDir(newName, id);
            SEMPLICE_TRY(m_playlists.setName(id, newName, dir, nowMs()));
            if (dir != playlist->dirName)
                m_ctx->report.fileOps.append({FileOp::Kind::MoveDir, kPlaylistsRoot + QLatin1Char('/') + playlist->dirName,
                                              kPlaylistsRoot + QLatin1Char('/') + dir});
            SEMPLICE_TRY(syncContainerPaths(ContainerKind::Playlist, id.value, false));
            if (!playlist->coverPath.isEmpty())
                SEMPLICE_TRY(m_playlists.setCover(id, kPlaylistsRoot + QLatin1Char('/') + dir + QLatin1Char('/') +
                                                          QFileInfo(playlist->coverPath).fileName(),
                                                  nowMs()));
        }
        if (patch.coverPath) {
            SEMPLICE_ASSIGN(fresh, m_playlists.get(id));
            if (patch.coverPath->isEmpty()) {
                if (!fresh->coverPath.isEmpty())
                    m_ctx->report.fileOps.append({FileOp::Kind::Trash, fresh->coverPath, {}});
                SEMPLICE_TRY(m_playlists.setCover(id, QString(), nowMs()));
            } else {
                SEMPLICE_ASSIGN(stored, storeCover(kPlaylistsRoot + QLatin1Char('/') + fresh->dirName, QStringLiteral("playlist"),
                                                   *patch.coverPath, fresh->coverPath));
                SEMPLICE_TRY(m_playlists.setCover(id, stored, nowMs()));
            }
        }
        if (patch.trackOrder) {
            SEMPLICE_ASSIGN(current, m_playlists.tracks(id));
            QList<TrackId> a = current;
            QList<TrackId> b = *patch.trackOrder;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            if (a != b)
                return Error::make(ErrorCode::InvalidArgument, QStringLiteral("track order must list exactly the playlist's tracks"));
            SEMPLICE_TRY(m_playlists.reorder(id, *patch.trackOrder));
        }
        touch(*m_ctx, id);
        return {};
    }, job);
}

Result<OperationReport> LibraryService::updateTrack(TrackId id, const TrackPatch& patch, const JobContext& job)
{
    SEMPLICE_ASSIGN(track, m_tracks.get(id));
    if (!track)
        return Error::make(ErrorCode::NotFound, QStringLiteral("track"));
    if (patch.title)
        SEMPLICE_TRY(validateTitle(*patch.title));
    if (patch.tags)
        SEMPLICE_TRY(validateTags(*patch.tags));
    if (patch.credits && patch.credits->isEmpty())
        return Error::make(ErrorCode::NoArtist, QStringLiteral("a track needs at least one artist"));

    return transact(false, [&]() -> Result<void> {
        if (patch.title)
            SEMPLICE_TRY(m_tracks.setTitle(id, *patch.title, nowMs()));
        if (patch.credits) {
            SEMPLICE_ASSIGN(before, m_tracks.credits(id));
            QList<TrackCredit> rows;
            for (const CreditDraft& c : *patch.credits) {
                SEMPLICE_ASSIGN(artistId, findOrCreateArtist(c.artist, c.name, false));
                if (std::any_of(rows.begin(), rows.end(), [&](const TrackCredit& r) { return r.artist == artistId; }))
                    continue;
                rows.append({artistId, static_cast<int>(rows.size()), c.role});
                m_ctx->creditTouched.insert(artistId);
                touch(*m_ctx, artistId);
            }
            SEMPLICE_TRY(m_tracks.setCredits(id, rows));
            for (const TrackCredit& old : std::as_const(before)) {  // a dropped credit may leave a hidden artist unreferenced
                m_ctx->creditTouched.insert(old.artist);
                touch(*m_ctx, old.artist);
            }
        }
        if (patch.tags)
            SEMPLICE_TRY(m_tags.setTags(TagOwner::Track, id.value, *patch.tags));
        // file name = f(title, position, first artist): follow it
        SEMPLICE_TRY(syncTrackPaths({id}, true));
        markDirty(id);
        touch(*m_ctx, id);
        return {};
    }, job);
}

}  // namespace semplice

namespace semplice {

template <typename R>
R stepOf(const char* label, R result)
{
    if (!result.ok())
        return Error::make(result.error().code, QStringLiteral("merge step %1: %2").arg(QString::fromLatin1(label), result.error().devMessage));
    return result;
}

// Merging goes through the same public operations as every other edit (each one journaled and consistent), so a
// failure half way leaves a valid library with part of the move done, never a broken one.
Result<void> LibraryService::mergeArtistInto(ArtistId source, ArtistId target, const JobContext& job)
{
    if (source == target)
        return {};
    SEMPLICE_ASSIGN(from, m_artists.get(source));
    SEMPLICE_ASSIGN(into, m_artists.get(target));
    if (!from || !into)
        return Error::make(ErrorCode::NotFound, QStringLiteral("artist"));

    // 1. albums the source is the main author of move under the target (the service handles folders and "(2)")
    SEMPLICE_ASSIGN(owned, m_albums.byPrimary(source));
    for (const Album& a : std::as_const(owned)) {
        if (a.isUnsorted)
            continue;
        AlbumPatch patch;
        patch.primaryArtist = target;
        SEMPLICE_TRY(stepOf("1", updateAlbum(a.id, patch, job)));
    }
    // 2. albums the source only co-authors: the target takes its place (once)
    SEMPLICE_ASSIGN(shared, m_albums.byCoAuthor(source));
    for (const Album& a : std::as_const(shared)) {
        SEMPLICE_ASSIGN(current, m_albums.get(a.id));
        if (!current)
            continue;
        SEMPLICE_ASSIGN(coAuthors, m_albums.coAuthors(a.id));
        QList<ArtistId> next;
        for (const CoAuthor& c : std::as_const(coAuthors)) {
            const ArtistId who = c.artist == source ? target : c.artist;
            if (who != current->primaryArtist && !next.contains(who))
                next.append(who);
        }
        AlbumPatch patch;
        patch.coAuthors = next;
        SEMPLICE_TRY(stepOf("2", updateAlbum(a.id, patch, job)));
    }
    // 3. the tracks of the Unsorted album of the source go to the Unsorted album of the target
    for (const Album& a : std::as_const(owned)) {
        if (!a.isUnsorted)
            continue;
        SEMPLICE_ASSIGN(tracks, m_albums.tracks(a.id));
        for (TrackId t : std::as_const(tracks))
            SEMPLICE_TRY(stepOf("3", addToDiscography(t, target)));
    }
    // 4. every credit of the source becomes a credit of the target
    SEMPLICE_ASSIGN(credited, m_tracks.creditedTo(source));
    for (TrackId t : std::as_const(credited)) {
        SEMPLICE_ASSIGN(credits, m_tracks.credits(t));
        QList<CreditDraft> next;
        QList<ArtistId> seen;
        for (const TrackCredit& c : std::as_const(credits)) {
            const ArtistId who = c.artist == source ? target : c.artist;
            if (seen.contains(who))
                continue;
            seen.append(who);
            next.append(CreditDraft(who, c.role));
        }
        TrackPatch patch;
        patch.credits = next;
        SEMPLICE_TRY(stepOf("4", updateTrack(t, patch, job)));
    }
    // 5. the tags of both (the target keeps its own first)
    SEMPLICE_ASSIGN(targetTags, m_tags.tags(TagOwner::Artist, target.value));
    SEMPLICE_ASSIGN(sourceTags, m_tags.tags(TagOwner::Artist, source.value));
    QStringList merged = targetTags;
    for (const QString& t : std::as_const(sourceTags))
        if (!merged.contains(t))
            merged.append(t);
    if (merged != targetTags) {
        ArtistPatch patch;
        patch.tags = merged;
        SEMPLICE_TRY(stepOf("5", updateArtist(target, patch, job)));
    }
    // 6. nothing refers to the source any more (the service may already have removed it with its last album)
    if (auto left = m_artists.get(source); left.ok() && left.value())
        SEMPLICE_TRY(stepOf("6", deleteArtist(source)));
    return {};
}

}  // namespace semplice
