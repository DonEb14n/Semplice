#include "core/playback/LibraryPlayback.h"

#include "core/db/AlbumRepo.h"
#include "core/db/ArtistRepo.h"
#include "core/db/PlaylistRepo.h"
#include "core/db/TagRepo.h"
#include "core/db/TrackRepo.h"
#include "core/playback/PlaybackService.h"
#include "core/tags/TagService.h"

#include "core/util/Logger.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>

namespace semplice {

namespace {
// A statistics or session write must never interrupt the music: log and carry on.
void logFailure(const Result<void>& result)
{
    if (!result.ok())
        SLOG_WARN(QStringLiteral("playback store write failed: ") + result.error().devMessage);
}
}  // namespace

std::optional<PlayableTrack> LibraryTrackCatalog::resolve(TrackId id)
{
    TrackRepo tracks(m_db);
    auto row = tracks.get(id);
    if (!row.ok() || !row.value())
        return std::nullopt;
    const Track& t = *row.value();
    PlayableTrack out;
    out.id = t.id;
    out.title = t.title;
    out.durationMs = t.durationMs;
    out.hasEmbeddedCover = t.hasEmbeddedCover;
    out.homeKind = t.home.kind;
    out.homeId = t.home.id;
    out.absolutePath = QDir(m_root).filePath(t.filePath);
    if (!QFileInfo(out.absolutePath).isFile())
        return std::nullopt;

    ArtistRepo artists(m_db);
    QStringList names;
    if (auto credits = tracks.credits(id); credits.ok())
        for (const TrackCredit& c : credits.value())
            if (auto a = artists.get(c.artist); a.ok() && a.value()) {
                names << a.value()->name;
                out.artistNames.append(a.value()->name);
                out.artistIds.append(c.artist.value);
            }
    out.artist = names.join(QStringLiteral(", "));

    if (t.home.kind == ContainerKind::Album) {
        AlbumRepo albums(m_db);
        if (auto a = albums.get(AlbumId(t.home.id)); a.ok() && a.value()) {
            out.albumTitle = a.value()->title;
            if (!a.value()->coverPath.isEmpty())
                out.coverPath = QDir(m_root).filePath(a.value()->coverPath);
        }
    } else {
    }
    return out;
}

void LibraryPlaybackStore::recordPlay(TrackId id, qint64 nowMs)
{
    logFailure(TrackRepo(m_db).recordPlay(id, nowMs));
}

void LibraryPlaybackStore::touchRecent(ContainerKind kind, qint64 id, qint64 nowMs)
{
    logFailure(RecentRepo(m_db).touch(kind, id, nowMs));
}

QJsonObject LibraryPlaybackStore::loadSession()
{
    auto text = KvRepo(m_db).get(QString::fromLatin1(kSessionKey));
    if (!text.ok() || !text.value())
        return {};
    return QJsonDocument::fromJson(text.value()->toUtf8()).object();
}

void LibraryPlaybackStore::saveSession(const QJsonObject& session)
{
    KvRepo kv(m_db);
    if (session.isEmpty())
        logFailure(kv.remove(QString::fromLatin1(kSessionKey)));
    else
        logFailure(kv.set(QString::fromLatin1(kSessionKey), QString::fromUtf8(QJsonDocument(session).toJson(QJsonDocument::Compact))));
}

// ------------------------------------------------------------------------------------------------

SystemIntegration::SystemIntegration(PlaybackService& service, ISystemMediaControls* controls, IThumbBar* thumbBar, QObject* parent)
    : QObject(parent), m_service(service), m_controls(controls), m_thumbBar(thumbBar)
{
    connect(&service, &PlaybackService::currentChanged, this, &SystemIntegration::refresh);
    connect(&service, &PlaybackService::stateChanged, this, &SystemIntegration::refresh);
    connect(&service, &PlaybackService::queueChanged, this, &SystemIntegration::refresh);
    connect(&service, &PlaybackService::modesChanged, this, &SystemIntegration::refresh);
    if (controls) {
        connect(controls, &ISystemMediaControls::playRequested, &service, qOverload<>(&PlaybackService::play));
        connect(controls, &ISystemMediaControls::pauseRequested, &service, &PlaybackService::pause);
        connect(controls, &ISystemMediaControls::toggleRequested, &service, &PlaybackService::toggle);
        connect(controls, &ISystemMediaControls::nextRequested, &service, &PlaybackService::next);
        connect(controls, &ISystemMediaControls::previousRequested, &service, &PlaybackService::previous);
    }
    if (thumbBar) {
        connect(thumbBar, &IThumbBar::previousClicked, &service, &PlaybackService::previous);
        connect(thumbBar, &IThumbBar::playPauseClicked, &service, &PlaybackService::toggle);
        connect(thumbBar, &IThumbBar::nextClicked, &service, &PlaybackService::next);
    }
    refresh();
}

// A picture inside the track (when there is no cover file) is read once per track for the Windows media panel.
QByteArray SystemIntegration::coverDataOf(const PlayableTrack& track)
{
    if (!track.coverPath.isEmpty() || !track.hasEmbeddedCover)
        return {};
    if (m_coverTrack != track.id.value) {
        m_coverTrack = track.id.value;
        m_cover.clear();
        PortableAtomicReplace replace;
        TagService tags(replace);
        auto cover = tags.readCover(track.absolutePath);
        if (cover.ok() && cover.value())
            m_cover = cover.value()->data;
    }
    return m_cover;
}

void SystemIntegration::refresh()
{
    QElapsedTimer timing;
    timing.start();
    struct Done {
        QElapsedTimer& t;
        ~Done()
        {
            if (t.elapsed() > 30)
                SLOG_WARN(QStringLiteral("SystemIntegration::refresh took %1 ms on the GUI thread").arg(t.elapsed()));
        }
    } done{timing};
    const bool hasTrack = m_service.currentTrack().has_value();
    const bool playing = m_service.state() == PlayState::Playing;
    const bool canPrev = m_service.canPrevious();
    const bool canNext = m_service.canNext();
    const PlayableTrack* current = hasTrack ? &*m_service.currentTrack() : nullptr;
    const bool sameMeta = m_sent && hasTrack == m_sentHasTrack && (!current || (current->id.value == m_sentTrack && current->title == m_sentTitle &&
                                                                              current->artist == m_sentArtist && current->albumTitle == m_sentAlbum &&
                                                                              current->coverPath == m_sentCoverPath));
    const bool sameState = m_sent && m_service.state() == m_sentState && canPrev == m_sentPrev && canNext == m_sentNext && hasTrack == m_sentHasTrack;
    if (sameMeta && sameState)
        return;
    if (m_controls && !sameMeta) {
        QElapsedTimer step;
        step.start();
        if (hasTrack) {
            const PlayableTrack& t = *m_service.currentTrack();
            const QByteArray cover = coverDataOf(t);
            if (step.elapsed() > 30)
                SLOG_WARN(QStringLiteral("refresh: reading the cover took %1 ms").arg(step.elapsed()));
            step.restart();
            m_controls->setMetadata({t.title, t.artist, t.albumTitle, t.coverPath, cover});
            if (step.elapsed() > 30)
                SLOG_WARN(QStringLiteral("refresh: setMetadata took %1 ms").arg(step.elapsed()));
        } else {
            m_controls->setMetadata({});
        }
    }
    if (m_controls && !sameState) {
        QElapsedTimer step;
        step.start();
        m_controls->setState(m_service.state());
        if (step.elapsed() > 30)
            SLOG_WARN(QStringLiteral("refresh: SMTC setState took %1 ms").arg(step.elapsed()));
        step.restart();
        m_controls->setEnabled(canPrev, canNext, hasTrack);
        if (step.elapsed() > 30)
            SLOG_WARN(QStringLiteral("refresh: SMTC setEnabled took %1 ms").arg(step.elapsed()));
    }
    m_sent = true;
    m_sentHasTrack = hasTrack;
    m_sentState = m_service.state();
    m_sentPrev = canPrev;
    m_sentNext = canNext;
    if (current) {
        m_sentTrack = current->id.value;
        m_sentTitle = current->title;
        m_sentArtist = current->artist;
        m_sentAlbum = current->albumTitle;
        m_sentCoverPath = current->coverPath;
    }
    if (m_thumbBar) {
        QElapsedTimer step;
        step.start();
        m_thumbBar->setState(playing, canPrev, canNext, hasTrack);
        if (step.elapsed() > 30)
            SLOG_WARN(QStringLiteral("refresh: thumbnail buttons took %1 ms").arg(step.elapsed()));
    }
}

}  // namespace semplice
