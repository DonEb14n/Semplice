#include "ui/LibraryEngine.h"

#include "core/db/ArtistRepo.h"
#include "core/db/TagRepo.h"
#include "core/util/Logger.h"

#include <QFileInfo>
#include <QVariantList>

namespace semplice {

namespace {

ReleaseDate dateOf(const QVariantMap& c)
{
    return {c.value(QStringLiteral("year")).toInt(), c.value(QStringLiteral("month")).toInt(), c.value(QStringLiteral("day")).toInt()};
}

// "rows": [{ index, title, names: [..], roles: [..] }, ...] -> forms
QList<ImportTrackForm> formsOf(const QVariantMap& c)
{
    QList<ImportTrackForm> out;
    for (const QVariant& v : c.value(QStringLiteral("rows")).toList()) {
        const QVariantMap r = v.toMap();
        ImportTrackForm f;
        f.scanIndex = r.value(QStringLiteral("index")).toInt();
        f.title = r.value(QStringLiteral("title")).toString();
        const QVariantList names = r.value(QStringLiteral("names")).toList();
        const QVariantList roles = r.value(QStringLiteral("roles")).toList();
        for (int i = 0; i < names.size(); ++i)
            f.credits.append(CreditDraft(names.at(i).toString(), roles.value(i).toInt() == 1 ? CreditRole::Feat : CreditRole::Main));
        out.append(f);
    }
    return out;
}

OperationResult resultFor(int token, const QString& entity, qint64 id, const Error* error)
{
    OperationResult r;
    r.token = token;
    r.entity = entity;
    r.id = id;
    r.createdId = id;
    if (error) {
        r.code = error->numericCode();
        r.message = error->devMessage;
    }
    return r;
}

}  // namespace

JobContext LibraryEngine::jobFor(int token)
{
    JobContext job;
    job.cancel = m_stop.get_token();
    job.progress = [this, token](const JobProgress& p) { emit importProgress(token, p.phase, p.done, p.total); };
    return job;
}

void LibraryEngine::clearScan()
{
    m_scan = {};
    m_proposal = {};
}

void LibraryEngine::scanFiles(int token, const QStringList& paths, qint64 preselectedArtist, bool reset)
{
    ScanSummary summary;
    summary.token = token;
    if (!m_session) {
        emit scanReady(summary);
        return;
    }
    if (reset) {
        clearScan();
        m_stop = std::stop_source();
    }
    ImportService& importer = m_session->importer();
    auto fresh = importer.scan(paths, jobFor(token));
    if (!fresh.ok()) {
        emit scanReady(summary);
        return;
    }
    summary.skipped = static_cast<int>(fresh.value().skipped.size());
    const int offset = static_cast<int>(m_scan.tracks.size());
    const ImportProposal proposal = importer.propose(fresh.value(), preselectedArtist > 0 ? std::optional<ArtistId>(ArtistId(preselectedArtist)) : std::nullopt);
    if (reset) {
        m_proposal = proposal;
        summary.hasProposal = !fresh.value().tracks.isEmpty();
        summary.kind = proposal.kind == ImportKind::Album ? 0 : 1;
        summary.albumTitle = proposal.album.title;
        summary.authors = proposal.album.authors;
        {
            ArtistRepo known(m_session->database());
            for (const QString& name : proposal.album.authors) {
                ScanAuthor a;
                a.name = name;
                if (auto found = known.findByName(name); found.ok() && found.value()) {
                    a.id = found.value()->id.value;
                    a.name = found.value()->name;
                    a.avatar = found.value()->avatarPath;
                    a.hidden = found.value()->hidden;
                }
                summary.authorRefs.append(a);
            }
        }
        summary.albumType = static_cast<int>(proposal.album.type);
        summary.year = proposal.album.date.year;
        summary.month = proposal.album.date.month;
        summary.day = proposal.album.date.day;
        summary.tags = proposal.album.tags;
        summary.coverPath = proposal.kind == ImportKind::Album ? proposal.album.coverPath : proposal.playlist.coverPath;
        // loose files (a playlist proposal) dropped on "Add album": the picture of the first track is still the cover
        if (proposal.kind == ImportKind::Playlist && !fresh.value().tracks.isEmpty() && m_proposal.album.coverPath.isEmpty()) {
            const QString first = !proposal.playlist.tracks.isEmpty() ? fresh.value().tracks.at(proposal.playlist.tracks.first().scanIndex).sourcePath : fresh.value().tracks.first().sourcePath;
            m_proposal.album.embeddedCover = importer.embeddedCoverOf(first);
        }
        summary.embeddedCover = m_proposal.album.embeddedCover.has_value();
        summary.playlistName = proposal.playlist.name;
    }
    const QList<ImportTrackForm>& forms = proposal.kind == ImportKind::Album ? proposal.album.tracks : proposal.playlist.tracks;
    for (const ImportTrackForm& f : forms) {
        if (f.scanIndex < 0 || f.scanIndex >= fresh.value().tracks.size())
            continue;
        const ScannedTrack& st = fresh.value().tracks.at(f.scanIndex);
        ScanRow row;
        row.index = offset + f.scanIndex;
        row.path = st.sourcePath;
        row.fileName = QFileInfo(st.sourcePath).fileName();
        row.title = f.title;
        for (const CreditDraft& cr : f.credits) {
            row.creditNames << cr.name;
            row.creditRoles << (cr.role == CreditRole::Feat ? 1 : 0);
        }
        row.durationMs = st.tags.durationMs;
        row.duplicate = st.duplicateOf.has_value();
        if (st.duplicateOf) {
            if (auto t = TrackRepo(m_session->database()).get(*st.duplicateOf); t.ok() && t.value())
                row.duplicateTitle = t.value()->title;
        }
        summary.rows.append(row);
    }
    for (const ScannedTrack& t : std::as_const(fresh.value().tracks))
        m_scan.tracks.append(t);
    for (const SkippedFile& s : std::as_const(fresh.value().skipped))
        m_scan.skipped.append(s);
    emit scanReady(summary);
}

void LibraryEngine::saveEntity(int token, const QVariantMap& command)
{
    OperationResult result;
    if (!m_session) {
        result.token = token;
        result.code = static_cast<int>(ErrorCode::NotFound);
        emit operationFinished(result);
        return;
    }
    if (command.value(QStringLiteral("startOver")).toBool())
        m_stop = std::stop_source();
    const QString type = command.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("artist"))
        result = saveArtist(token, command);
    else if (type == QLatin1String("album"))
        result = saveAlbum(token, command);
    else if (type == QLatin1String("playlist"))
        result = savePlaylist(token, command);
    else if (type == QLatin1String("track"))
        result = saveTrack(token, command);
    else
        result = resultFor(token, type, 0, nullptr);
    emit operationFinished(result);
}

// ---- artist: { type, mode: add|edit, id, name, avatar, removeAvatar, tags, hidden } -------------------------

OperationResult LibraryEngine::saveArtist(int token, const QVariantMap& c)
{
    LibraryService& lib = m_session->library();
    const bool edit = c.value(QStringLiteral("mode")).toString() == QLatin1String("edit");
    if (!edit) {
        ArtistDraft draft;
        draft.name = c.value(QStringLiteral("name")).toString();
        draft.hidden = c.value(QStringLiteral("hidden")).toBool();
        draft.avatarPath = c.value(QStringLiteral("avatar")).toString();
        draft.tags = c.value(QStringLiteral("tags")).toStringList();
        auto id = lib.createArtist(draft);
        if (!id.ok())
            return resultFor(token, QStringLiteral("artist"), 0, &id.error());
        return resultFor(token, QStringLiteral("artist"), id.value().value, nullptr);
    }
    const ArtistId id(c.value(QStringLiteral("id")).toLongLong());
    ArtistRepo artists(m_session->database());
    auto current = artists.get(id);
    if (!current.ok() || !current.value()) {
        const Error e = Error::make(ErrorCode::NotFound, QStringLiteral("artist"));
        return resultFor(token, QStringLiteral("artist"), 0, &e);
    }
    ArtistPatch patch;
    const QString name = c.value(QStringLiteral("name")).toString();
    if (name.trimmed() != current.value()->name)
        patch.name = name;
    if (c.value(QStringLiteral("removeAvatar")).toBool())
        patch.avatarPath = QString();
    else if (!c.value(QStringLiteral("avatar")).toString().isEmpty())
        patch.avatarPath = c.value(QStringLiteral("avatar")).toString();
    const QStringList tags = c.value(QStringLiteral("tags")).toStringList();
    if (auto old = TagRepo(m_session->database()).tags(TagOwner::Artist, id.value); old.ok() && QSet<QString>(old.value().begin(), old.value().end()) != QSet<QString>(tags.begin(), tags.end()))
        patch.tags = tags;
    if (c.contains(QStringLiteral("userHidden")))
        patch.userHidden = c.value(QStringLiteral("userHidden")).toBool();
    auto r = lib.updateArtist(id, patch, jobFor(token));
    if (!r.ok())
        return resultFor(token, QStringLiteral("artist"), 0, &r.error());
    return resultFor(token, QStringLiteral("artist"), id.value, nullptr);
}

// ---- album: { type, mode, id, title, albumType, year, month, day, tags, cover, removeCover,
//               authors: [{id, name}], rows: [{index, title, names, roles}] } --------------------------------

OperationResult LibraryEngine::saveAlbum(int token, const QVariantMap& c)
{
    LibraryService& lib = m_session->library();
    const bool edit = c.value(QStringLiteral("mode")).toString() == QLatin1String("edit");
    const QString entity = QStringLiteral("album");
    QStringList authorNames;
    QList<ArtistId> authorIds;
    ArtistRepo artists(m_session->database());
    for (const QVariant& v : c.value(QStringLiteral("authors")).toList()) {
        const QVariantMap a = v.toMap();
        authorNames << a.value(QStringLiteral("name")).toString();
        authorIds << ArtistId(a.value(QStringLiteral("id")).toLongLong());
    }
    if (!edit) {
        ImportAlbumForm form = m_proposal.album;
        form.title = c.value(QStringLiteral("title")).toString();
        form.authors = authorNames;
        form.type = static_cast<AlbumType>(c.value(QStringLiteral("albumType")).toInt());
        form.date = dateOf(c);
        form.tags = c.value(QStringLiteral("tags")).toStringList();
        const QString cover = c.value(QStringLiteral("cover")).toString();
        if (!cover.isEmpty()) {
            form.coverPath = cover;
            form.embeddedCover.reset();
        } else if (c.value(QStringLiteral("removeCover")).toBool()) {
            form.coverPath.clear();
            form.embeddedCover.reset();
        }
        form.tracks = formsOf(c);
        auto done = m_session->importer().commitAlbum(m_scan, form, jobFor(token));
        if (!done.ok())
            return resultFor(token, entity, 0, &done.error());
        return resultFor(token, entity, done.value().album.value, nullptr);
    }

    const AlbumId id(c.value(QStringLiteral("id")).toLongLong());
    auto card = CardQueries(m_session->database()).album(id.value);
    if (!card.ok())
        return resultFor(token, entity, 0, &card.error());
    const AlbumCard& old = card.value();
    AlbumPatch patch;
    const QString title = c.value(QStringLiteral("title")).toString();
    if (title.trimmed() != old.title)
        patch.title = title;
    const AlbumType type = static_cast<AlbumType>(c.value(QStringLiteral("albumType")).toInt());
    if (type != old.type)
        patch.type = type;
    const ReleaseDate date = dateOf(c);
    if (!(date == old.date))
        patch.date = date;
    const QStringList tags = c.value(QStringLiteral("tags")).toStringList();
    if (QSet<QString>(tags.begin(), tags.end()) != QSet<QString>(old.tags.begin(), old.tags.end()))
        patch.tags = tags;
    if (c.value(QStringLiteral("removeCover")).toBool())
        patch.coverPath = QString();
    else if (!c.value(QStringLiteral("cover")).toString().isEmpty())
        patch.coverPath = c.value(QStringLiteral("cover")).toString();
    // authors: names without an id are created (visible: they author an album)
    QList<ArtistId> resolved;
    for (int i = 0; i < authorIds.size(); ++i) {
        ArtistId aid = authorIds.at(i);
        if (!aid.isValid()) {
            auto found = artists.findByName(authorNames.at(i));
            if (found.ok() && found.value()) {
                aid = found.value()->id;
            } else {
                auto made = lib.createArtist({authorNames.at(i), false, {}, {}});
                if (!made.ok())
                    return resultFor(token, entity, 0, &made.error());
                aid = made.value();
            }
        }
        resolved << aid;
    }
    if (!resolved.isEmpty()) {
        if (resolved.first().value != old.artists.value(0).id)
            patch.primaryArtist = resolved.first();
        QList<ArtistId> co = resolved.mid(1);
        QList<ArtistId> oldCo;
        for (int i = 1; i < old.artists.size(); ++i)
            oldCo << ArtistId(old.artists.at(i).id);
        if (co != oldCo)
            patch.coAuthors = co;
    }
    auto updated = lib.updateAlbum(id, patch, jobFor(token));
    if (!updated.ok())
        return resultFor(token, entity, 0, &updated.error());
    const QList<ImportTrackForm> forms = formsOf(c);
    if (!forms.isEmpty()) {
        auto appended = m_session->importer().appendToAlbum(id, m_scan, forms, jobFor(token));
        if (!appended.ok())
            return resultFor(token, entity, id.value, &appended.error());
    }
    return resultFor(token, entity, id.value, nullptr);
}

// ---- playlist: { type, mode, id, name, cover, removeCover, rows } --------------------------------------------

OperationResult LibraryEngine::savePlaylist(int token, const QVariantMap& c)
{
    LibraryService& lib = m_session->library();
    const bool edit = c.value(QStringLiteral("mode")).toString() == QLatin1String("edit");
    const QString entity = QStringLiteral("playlist");
    if (!edit) {
        // tracks that already live in the library (a track menu started this window) come as { trackId }
        PlaylistDraft draft;
        draft.name = c.value(QStringLiteral("name")).toString();
        draft.coverPath = c.value(QStringLiteral("cover")).toString();
        for (const QVariant& v : c.value(QStringLiteral("existing")).toList()) {
            TrackInput in;
            in.existing = TrackId(v.toLongLong());
            draft.tracks.append(in);
        }
        const QList<ImportTrackForm> forms = formsOf(c);
        if (draft.tracks.isEmpty()) {
            ImportPlaylistForm form = m_proposal.kind == ImportKind::Playlist ? m_proposal.playlist : ImportPlaylistForm{};
            form.name = draft.name;
            if (!draft.coverPath.isEmpty())
                form.coverPath = draft.coverPath;
            else if (c.value(QStringLiteral("removeCover")).toBool())
                form.coverPath.clear();
            form.tracks = forms;
            auto done = m_session->importer().commitPlaylist(m_scan, form, jobFor(token));
            if (!done.ok())
                return resultFor(token, entity, 0, &done.error());
            return resultFor(token, entity, done.value().playlist.value, nullptr);
        }
        auto id = lib.createPlaylist(draft, jobFor(token));
        if (!id.ok())
            return resultFor(token, entity, 0, &id.error());
        if (!forms.isEmpty())
            (void)m_session->importer().appendToPlaylist(id.value(), m_scan, forms, jobFor(token));
        return resultFor(token, entity, id.value().value, nullptr);
    }
    const PlaylistId id(c.value(QStringLiteral("id")).toLongLong());
    auto old = PlaylistRepo(m_session->database()).get(id);
    if (!old.ok() || !old.value()) {
        const Error e = Error::make(ErrorCode::NotFound, QStringLiteral("playlist"));
        return resultFor(token, entity, 0, &e);
    }
    PlaylistPatch patch;
    const QString name = c.value(QStringLiteral("name")).toString();
    if (name.trimmed() != old.value()->name)
        patch.name = name;
    if (c.value(QStringLiteral("removeCover")).toBool())
        patch.coverPath = QString();
    else if (!c.value(QStringLiteral("cover")).toString().isEmpty())
        patch.coverPath = c.value(QStringLiteral("cover")).toString();
    auto updated = lib.updatePlaylist(id, patch, jobFor(token));
    if (!updated.ok())
        return resultFor(token, entity, 0, &updated.error());
    const QList<ImportTrackForm> forms = formsOf(c);
    if (!forms.isEmpty()) {
        auto appended = m_session->importer().appendToPlaylist(id, m_scan, forms, jobFor(token));
        if (!appended.ok())
            return resultFor(token, entity, id.value, &appended.error());
    }
    return resultFor(token, entity, id.value, nullptr);
}

// ---- track: { type, id, title, credits: [{id, name, role}], tags } -----------------------------------------

OperationResult LibraryEngine::saveTrack(int token, const QVariantMap& c)
{
    const TrackId id(c.value(QStringLiteral("id")).toLongLong());
    TrackPatch patch;
    auto old = TrackRepo(m_session->database()).get(id);
    if (!old.ok() || !old.value()) {
        const Error e = Error::make(ErrorCode::NotFound, QStringLiteral("track"));
        return resultFor(token, QStringLiteral("track"), 0, &e);
    }
    const QString title = c.value(QStringLiteral("title")).toString();
    if (title.trimmed() != old.value()->title)
        patch.title = title;
    QList<CreditDraft> credits;
    for (const QVariant& v : c.value(QStringLiteral("credits")).toList()) {
        const QVariantMap m = v.toMap();
        const CreditRole role = m.value(QStringLiteral("role")).toInt() == 1 ? CreditRole::Feat : CreditRole::Main;
        const qint64 aid = m.value(QStringLiteral("id")).toLongLong();
        credits.append(aid > 0 ? CreditDraft(ArtistId(aid), role) : CreditDraft(m.value(QStringLiteral("name")).toString(), role));
    }
    if (!credits.isEmpty())
        patch.credits = credits;
    if (c.contains(QStringLiteral("tags")))
        patch.tags = c.value(QStringLiteral("tags")).toStringList();
    auto r = m_session->library().updateTrack(id, patch, jobFor(token));
    if (!r.ok())
        return resultFor(token, QStringLiteral("track"), 0, &r.error());
    return resultFor(token, QStringLiteral("track"), id.value, nullptr);
}

}  // namespace semplice

namespace semplice {

void LibraryEngine::loadTrackEdit(qint64 id)
{
    if (!m_session)
        return;
    Database& db = m_session->database();
    TrackRepo tracks(db);
    auto t = tracks.get(TrackId(id));
    if (!t.ok() || !t.value()) {
        emit cardFailed(14, id);
        return;
    }
    TrackEdit out;
    out.id = id;
    out.title = t.value()->title;
    ArtistRepo artists(db);
    if (auto credits = tracks.credits(TrackId(id)); credits.ok()) {
        for (const TrackCredit& c : credits.value()) {
            if (auto a = artists.get(c.artist); a.ok() && a.value()) {
                out.artistNames << a.value()->name;
                out.artistIds << a.value()->id.value;
                out.roles << (c.role == CreditRole::Feat ? 1 : 0);
                out.hidden << a.value()->hidden;
                out.avatars << a.value()->avatarPath;
            }
        }
    }
    if (auto tags = TagRepo(db).tags(TagOwner::Track, id); tags.ok())
        out.tags = tags.value();
    emit trackEditReady(out);
}

}  // namespace semplice

namespace semplice {

void LibraryEngine::planDelete(int op, qint64 id)
{
    DeleteSummary s;
    s.op = op;
    s.id = id;
    if (m_session) {
        LibraryService& lib = m_session->library();
        Result<DeletePlan> plan = Error::make(ErrorCode::NotFound, QStringLiteral("plan"));
        switch (static_cast<OperationKind>(op)) {
        case OperationKind::DeleteTrack: plan = lib.planDeleteTrack(TrackId(id)); break;
        case OperationKind::DeleteAlbum: plan = lib.planDeleteAlbum(AlbumId(id)); break;
        case OperationKind::DeleteArtist: plan = lib.planDeleteArtist(ArtistId(id)); break;
        case OperationKind::DeletePlaylist: plan = lib.planDeletePlaylist(PlaylistId(id)); break;
        default: break;
        }
        if (plan.ok()) {
            const DeletePlan& p = plan.value();
            s.ok = true;
            s.tracksDeleted = static_cast<int>(p.tracksDeleted.size());
            s.tracksKept = static_cast<int>(p.tracksKept.size());
            s.tracksMoved = static_cast<int>(p.tracksMoved.size());
            s.albumsDeleted = static_cast<int>(p.albumsDeleted.size());
            s.playlistsDeleted = static_cast<int>(p.playlistsDeleted.size());
            s.albumsRehomed = static_cast<int>(p.albumsRehomed.size());
            s.artistsDeleted = static_cast<int>(p.artistsDeleted.size());
            s.artistsHidden = static_cast<int>(p.artistsHidden.size());
        }
    }
    emit deletePlanReady(s);
}

}  // namespace semplice

namespace semplice {

void LibraryEngine::rescan(int token)
{
    if (!m_session) {
        emit rescanFinished(token, static_cast<int>(ErrorCode::NotFound), 0, false, false);
        return;
    }
    m_stop = std::stop_source();
    auto report = m_session->rescanner().run({}, jobFor(token));
    if (!report.ok()) {
        emit rescanFinished(token, report.error().numericCode(), 0, false, false);
        return;
    }
    emit libraryChanged(ChangeSet{});  // lists, Home and cards read the library again
    emit rescanFinished(token, 0, report.value().adopted, report.value().missingBlocked > 0, report.value().cancelled);
}

}  // namespace semplice

namespace semplice {

void LibraryEngine::findMergeTarget(int token, const QString& name, qint64 exceptId)
{
    bool exists = false;
    bool hidden = false;
    if (m_session) {
        if (auto found = ArtistRepo(m_session->database()).findByName(name); found.ok() && found.value() && found.value()->id.value != exceptId) {
            exists = true;
            hidden = found.value()->hidden;
        }
    }
    emit mergeTargetFound(token, exists, hidden);
}

}  // namespace semplice

namespace semplice {

void LibraryEngine::syncTags()
{
    if (!m_session)
        return;
    auto left = m_session->library().syncDirtyTags({}, &m_syncGaveUp);
    if (!left.ok())
        SLOG_WARN(QStringLiteral("tag sync: ") + left.error().devMessage);
}

}  // namespace semplice
