#include "core/playback/PlaybackService.h"

#include "core/util/Logger.h"

#include <QJsonDocument>

#include <algorithm>
#include <random>

namespace semplice {

namespace {

quint64 chooseSeed(quint64 given)
{
    if (given != 0)
        return given;
    std::random_device device;
    return (static_cast<quint64>(device()) << 32) ^ device();
}

qint64 originKey(ContainerKind kind, qint64 id)
{
    return (static_cast<qint64>(kind) << 50) | id;
}

}  // namespace

PlaybackService::PlaybackService(const Dependencies& deps, QObject* parent)
    : QObject(parent),
      m_backend(deps.backend),
      m_catalog(deps.catalog),
      m_store(deps.store),
      m_clock(deps.clock),
      m_recommender(deps.recommender ? deps.recommender : &m_none),
      m_settings(deps.settings),
      m_queue(chooseSeed(deps.randomSeed))
{
    if (m_settings) {
        m_volume = std::clamp(m_settings->volume(), 0.0, 1.0);
        m_muted = m_settings->muted();
    }
    connect(&m_backend, &IAudioBackend::positionChanged, this, &PlaybackService::onPosition);
    connect(&m_backend, &IAudioBackend::durationChanged, this, &PlaybackService::onDuration);
    connect(&m_backend, &IAudioBackend::mediaEnded, this, &PlaybackService::onEnded);
    connect(&m_backend, &IAudioBackend::errorOccurred, this, &PlaybackService::onError);
    applyVolume();
}

// ------------------------------------------------------------------------------------------ starting

bool PlaybackService::play(const PlayRequest& request)
{
    SEMPLICE_SLOW_SCOPE("PlaybackService::play");
    if (!m_queue.start(request, m_queue.shuffle(), m_queue.repeat())) {
        return false;
    }
    m_visitGroup = -1;
    m_registeredOrigins.clear();
    m_skipped = 0;
    loadCurrent(true);
    return m_state == PlayState::Playing;
}

void PlaybackService::restore()
{
    const QJsonObject saved = m_store.loadSession();
    if (saved.isEmpty())
        return;
    auto restored = PlayQueue::fromJson(saved.value(QStringLiteral("queue")).toObject());
    if (!restored)
        return;
    m_queue = std::move(*restored);

    QSet<TrackId> missing;
    for (int g = 0; g < m_queue.groupCount(); ++g)
        for (const QueueTrack& t : m_queue.group(g).tracks)
            if (!m_catalog.resolve(t.track))
                missing.insert(t.track);
    const auto removed = m_queue.removeTracks(missing);
    m_skipped = removed.removed;
    m_visitGroup = -1;
    m_registeredOrigins.clear();
    emit modesChanged();
    if (m_queue.isEmpty()) {
        if (m_skipped > 0)
            emit tracksSkipped(m_skipped);
        m_skipped = 0;
        m_current.reset();
        emit currentChanged();
        emit queueChanged();
        return;
    }
    const qint64 position = removed.currentRemoved ? 0 : static_cast<qint64>(saved.value(QStringLiteral("positionMs")).toDouble());
    loadCurrent(false);  // paused; handles a removed current track by moving on
    if (m_current && position > 0 && !removed.currentRemoved)
        seek(position);
}

void PlaybackService::persist()
{
    saveSession();
}

void PlaybackService::saveSession()
{
    if (m_queue.isEmpty()) {
        m_store.saveSession({});
        return;
    }
    m_store.saveSession(QJsonObject{{QStringLiteral("version"), 1},
                                    {QStringLiteral("queue"), m_queue.toJson()},
                                    {QStringLiteral("positionMs"), static_cast<double>(m_backend.positionMs())}});
}

// ------------------------------------------------------------------------------------------ loading

bool PlaybackService::handleStep(PlayQueue::Step step)
{
    switch (step) {
    case PlayQueue::Step::Moved: return true;
    case PlayQueue::Step::End: enterStopped(); return false;
    case PlayQueue::Step::NeedRecommendations:
        if (startRecommendations() && m_queue.advance(false) == PlayQueue::Step::Moved)
            return true;
        enterStopped();
        return false;
    }
    return false;
}

void PlaybackService::loadCurrent(bool autoplay)
{
    SEMPLICE_SLOW_SCOPE("PlaybackService::loadCurrent");
    for (;;) {
        const auto entry = m_queue.currentTrack();
        if (!entry) {
            if (m_queue.isEmpty()) {
                enterStopped();
                break;
            }
            if (!handleStep(m_queue.advance(false)))  // the current track was removed: carry on with the successor
                break;
            continue;
        }
        const auto info = m_catalog.resolve(entry->track);
        if (!info) {
            ++m_skipped;
            m_queue.removeTracks({entry->track});
            continue;
        }
        m_inLoad = true;
        m_loadFailed = false;
        m_loadingPath = info->absolutePath;
        m_backend.load(info->absolutePath);
        m_inLoad = false;
        if (m_loadFailed) {
            ++m_skipped;
            m_queue.removeTracks({entry->track});
            continue;
        }
        m_current = info;
        resetListening();
        if (m_visitGroup != m_queue.currentGroupIndex()) {
            m_visitGroup = m_queue.currentGroupIndex();
            m_registeredOrigins.clear();
        }
        applyVolume();
        if (autoplay)
            m_backend.play();
        setState(autoplay ? PlayState::Playing : PlayState::Paused);
        preloadNext();
        ensureLookahead();
        saveSession();
        break;
    }
    if (m_skipped > 0) {
        const int count = m_skipped;
        m_skipped = 0;
        emit tracksSkipped(count);  // one banner for the whole batch
    }
    emit currentChanged();
    emit queueChanged();
}

void PlaybackService::enterStopped()
{
    m_backend.stop();
    m_lastPos = 0;
    if (m_queue.isEmpty()) {
        m_current.reset();
        m_store.saveSession({});
    } else {
        saveSession();
    }
    setState(PlayState::Stopped);
}

QList<TrackId> PlaybackService::seedTracks() const
{
    QList<TrackId> seed;
    QList<TrackId> fallback;
    for (int g = 0; g < m_queue.groupCount(); ++g) {
        const QueueGroup& group = m_queue.group(g);
        for (const QueueTrack& t : group.tracks)
            (group.origin.kind == OriginKind::Recommendations ? fallback : seed).append(t.track);
    }
    return seed.isEmpty() ? fallback : seed;
}

bool PlaybackService::startRecommendations()
{
    RecommendationRequest request;
    request.seedTracks = seedTracks();
    for (int g = 0; g < m_queue.groupCount(); ++g)
        if (m_queue.group(g).origin.kind == OriginKind::Recommendations)
            for (const QueueTrack& t : m_queue.group(g).tracks)
                request.exclude.append(t.track);
    if (const auto cur = m_queue.currentTrack())
        request.exclude.append(cur->track);
    request.count = kLookAhead;
    QueueGroup group;
    group.origin = {OriginKind::Recommendations, 0, QString()};
    for (TrackId id : m_recommender->pick(request))
        if (m_catalog.resolve(id))
            group.tracks.append({id});
    if (group.tracks.isEmpty())
        return false;
    m_queue.appendGroup(group);
    return true;
}

void PlaybackService::ensureLookahead()
{
    if (m_queue.isEmpty() || m_queue.currentOrigin().kind != OriginKind::Recommendations)
        return;
    const int have = static_cast<int>(m_queue.upNext(kLookAhead).size());
    if (have >= kLookAhead)
        return;
    RecommendationRequest request;
    request.seedTracks = seedTracks();
    for (const QueueTrack& t : m_queue.group(m_queue.currentGroupIndex()).tracks)
        request.exclude.append(t.track);
    request.count = kLookAhead - have;
    QList<QueueTrack> add;
    for (TrackId id : m_recommender->pick(request))
        if (m_catalog.resolve(id))
            add.append({id});
    if (!add.isEmpty())
        m_queue.appendToGroup(m_queue.currentGroupIndex(), add);
}

void PlaybackService::preloadNext()
{
    if (const auto next = m_queue.peekNext())
        if (const auto info = m_catalog.resolve(next->track))
            m_backend.preload(info->absolutePath);
}

// ------------------------------------------------------------------------------------------ transport

void PlaybackService::play()
{
    switch (m_state) {
    case PlayState::Playing: return;
    case PlayState::Paused:
        m_backend.play();
        setState(PlayState::Playing);
        return;
    case PlayState::Stopped:
        if (!m_queue.isEmpty())
            loadCurrent(true);
        return;
    }
}

void PlaybackService::pause()
{
    if (m_state != PlayState::Playing)
        return;
    m_backend.pause();
    setState(PlayState::Paused);
    saveSession();
}

void PlaybackService::toggle()
{
    if (m_state == PlayState::Playing)
        pause();
    else
        play();
}

void PlaybackService::stop()
{
    if (m_state == PlayState::Stopped)
        return;
    enterStopped();
}

bool PlaybackService::canNext() const
{
    if (m_queue.isEmpty())
        return false;
    return !m_queue.upNext(1).isEmpty() || m_queue.repeat() == RepeatMode::All || m_queue.afterEnd() == AfterEnd::Recommend;
}

bool PlaybackService::canPrevious() const
{
    return !m_queue.isEmpty() && (positionMs() > kPreviousRestartMs || m_queue.canPrevious());
}

void PlaybackService::next()
{
    if (!canNext())
        return;
    if (handleStep(m_queue.advance(false)))
        loadCurrent(true);
}

void PlaybackService::previous()
{
    if (m_queue.isEmpty())
        return;
    if (positionMs() > kPreviousRestartMs) {
        seek(0);
        return;
    }
    if (m_queue.goPrevious())
        loadCurrent(true);
}

bool PlaybackService::playAt(int group, int index)
{
    if (!m_queue.jumpTo(group, index))
        return false;
    loadCurrent(true);
    return true;
}

void PlaybackService::seek(qint64 positionMs)
{
    if (!m_current)
        return;
    positionMs = std::max<qint64>(0, positionMs);
    m_backend.seek(positionMs);
    m_lastPos = positionMs;  // a jump is not listening time
    emit positionChanged(positionMs);
}

// ------------------------------------------------------------------------------------------ modes

void PlaybackService::setShuffle(bool on)
{
    if (on == m_queue.shuffle())
        return;
    m_queue.setShuffle(on);
    emit modesChanged();
    emit queueChanged();
    preloadNext();
    saveSession();
}

void PlaybackService::setRepeat(RepeatMode mode)
{
    if (mode == m_queue.repeat())
        return;
    m_queue.setRepeat(mode);
    emit modesChanged();
    preloadNext();
    saveSession();
}

void PlaybackService::setVolume(double volume01)
{
    m_volume = std::clamp(volume01, 0.0, 1.0);
    if (m_settings)
        m_settings->setVolume(m_volume);
    applyVolume();
    emit volumeChanged();
}

void PlaybackService::setMuted(bool muted)
{
    m_muted = muted;
    if (m_settings)
        m_settings->setMuted(muted);
    applyVolume();
    emit volumeChanged();
}

void PlaybackService::applyVolume()
{
    m_backend.setVolume(m_volume);
    m_backend.setMuted(m_muted);
}

// ------------------------------------------------------------------------------------------ state

std::optional<QueueOrigin> PlaybackService::currentOrigin() const
{
    if (m_queue.isEmpty() || !m_queue.currentTrack())
        return std::nullopt;
    return m_queue.currentOrigin();
}

qint64 PlaybackService::positionMs() const
{
    return m_current ? m_backend.positionMs() : 0;
}

qint64 PlaybackService::durationMs() const
{
    if (!m_current)
        return 0;
    const qint64 fromBackend = m_backend.durationMs();
    return fromBackend > 0 ? fromBackend : m_current->durationMs;
}

void PlaybackService::setState(PlayState state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void PlaybackService::revalidate(const QSet<TrackId>& touched)
{
    SEMPLICE_SLOW_SCOPE("PlaybackService::revalidate");
    if (m_queue.isEmpty())
        return;
    // A rename or delete made the player let go of its file (Windows does not allow it while it is open): when the
    // track still exists, load it from its new place and go on from where it was.
    const auto released = m_backend.takeReleased();
    auto relocate = [&] {
        if (!released || m_state == PlayState::Stopped)
            return;
        const auto entry = m_queue.currentTrack();
        const auto info = entry ? m_catalog.resolve(entry->track) : std::nullopt;
        if (!info)
            return;
        m_current = info;
        m_inLoad = true;
        m_loadFailed = false;
        m_loadingPath = info->absolutePath;
        m_backend.load(info->absolutePath);
        m_inLoad = false;
        m_backend.seek(released->positionMs);
        if (released->playing)
            m_backend.play();
        emit currentChanged();
    };
    QSet<TrackId> gone;
    for (TrackId id : touched)
        if (!m_catalog.resolve(id))
            gone.insert(id);
    const auto removed = m_queue.removeTracks(gone);
    if (removed.removed == 0)
    {
        relocate();
        return;
    }
    if (m_queue.isEmpty()) {
        m_current.reset();
        enterStopped();
        emit currentChanged();
        emit queueChanged();
        return;
    }
    if (removed.currentRemoved) {
        if (m_state == PlayState::Stopped) {
            emit queueChanged();
            return;
        }
        loadCurrent(m_state == PlayState::Playing);
        return;
    }
    relocate();
    preloadNext();
    ensureLookahead();
    saveSession();
    emit queueChanged();
}

// ------------------------------------------------------------------------------------------ backend events

void PlaybackService::onDuration(qint64 durationMs)
{
    emit durationChanged(durationMs);
}

void PlaybackService::onPosition(qint64 positionMs)
{
    SEMPLICE_SLOW_SCOPE("PlaybackService::onPosition");
    emit positionChanged(positionMs);
    if (m_state != PlayState::Playing || !m_current)
        return;
    const qint64 delta = positionMs - m_lastPos;
    if (delta > 0 && delta <= kMaxPositionStepMs)
        m_listenedMs += delta;
    m_lastPos = positionMs;
    evaluateListening();
}

void PlaybackService::onEnded()
{
    if (m_state != PlayState::Playing || !m_current)
        return;
    // the last stretch before the end may not have produced a position update
    const qint64 rest = durationMs() - m_lastPos;
    if (rest > 0 && rest <= kMaxPositionStepMs)
        m_listenedMs += rest;
    evaluateListening();
    if (handleStep(m_queue.advance(true)))
        loadCurrent(true);
}

void PlaybackService::onError(const QString& path, const QString& message)
{
    if (m_inLoad && path == m_loadingPath) {
        m_loadFailed = true;
        SLOG_WARN(QStringLiteral("cannot play %1: %2").arg(path, message));
        return;
    }
    if (!m_current || path != m_current->absolutePath || m_state == PlayState::Stopped)
        return;
    SLOG_WARN(QStringLiteral("playback error in %1: %2").arg(path, message));
    ++m_skipped;
    m_queue.removeTracks({m_current->id});
    loadCurrent(m_state == PlayState::Playing);
}

// ------------------------------------------------------------------------------------------ statistics

void PlaybackService::resetListening()
{
    m_listenedMs = 0;
    m_lastPos = 0;
    m_counted = false;
}

void PlaybackService::evaluateListening()
{
    if (m_counted || !m_current)
        return;
    const qint64 duration = durationMs();
    const qint64 needed = duration > 0 ? std::min(kStatsMaxMs, duration / 2) : kStatsMaxMs;
    if (m_listenedMs < needed)
        return;
    m_counted = true;
    m_store.recordPlay(m_current->id, nowMs());
    if (const auto entry = m_queue.currentTrack())
        registerRecent(*entry);
}

void PlaybackService::registerRecent(const QueueTrack& entry)
{
    const QueueOrigin& origin = m_queue.currentOrigin();
    if (origin.kind == OriginKind::Recommendations)
        return;  // the endless stream never touches Recently Played
    ContainerKind kind = ContainerKind::Album;
    qint64 id = 0;
    if (entry.sourceId > 0) {
        kind = entry.sourceKind;
        id = entry.sourceId;
    } else if (origin.kind == OriginKind::Album) {
        id = origin.id;
    } else if (origin.kind == OriginKind::Playlist) {
        kind = ContainerKind::Playlist;
        id = origin.id;
    }
    if (id <= 0)
        return;
    if (!m_registeredOrigins.contains(originKey(kind, id))) {
        m_registeredOrigins.insert(originKey(kind, id));
        m_store.touchRecent(kind, id, nowMs());
        emit recentlyPlayedChanged();
    }
}

}  // namespace semplice

namespace semplice {

void PlaybackService::previousTrack()
{
    if (!m_queue.isEmpty() && m_queue.goPrevious())
        loadCurrent(true);
}

}  // namespace semplice
