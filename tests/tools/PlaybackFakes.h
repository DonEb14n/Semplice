#pragma once

#include "core/playback/PlaybackInterfaces.h"
#include "core/services/ISettings.h"
#include "core/util/Clock.h"

#include <QHash>
#include <QTimeZone>
#include <QPair>
#include <QSet>

#include <algorithm>
#include <functional>

namespace semplice::testing {

class StepClock final : public IClock {
public:
    QDateTime now() const override { return QDateTime(QDate(2026, 10, 19), QTime(12, 0), QTimeZone::UTC).addMSecs(m_ms); }
    void advance(qint64 ms) { m_ms += ms; }

private:
    qint64 m_ms = 0;
};

// Scriptable audio backend: tests drive time (`advance`) and the end of a track (`finish`) by hand.
class FakeAudioBackend final : public IAudioBackend {
    Q_OBJECT
public:
    using IAudioBackend::IAudioBackend;

    void load(const QString& path) override
    {
        loads << path;
        loaded = path;
        m_position = 0;
        m_playing = false;
        m_duration = durations.value(path, durationProvider ? durationProvider(path) : defaultDuration);
        if (failOnLoad.contains(path)) {
            loaded.clear();
            emit errorOccurred(path, QStringLiteral("cannot open"));  // synchronously, like a bad file
            return;
        }
        emit durationChanged(m_duration);
    }
    void play() override { m_playing = true; ++playCalls; }
    void pause() override { m_playing = false; ++pauseCalls; }
    void stop() override
    {
        m_playing = false;
        m_position = 0;
        ++stopCalls;
    }
    void seek(qint64 ms) override
    {
        m_position = ms;
        seeks << ms;
    }
    void setVolume(double v) override { volume = v; }
    void setMuted(bool m) override { muted = m; }
    qint64 positionMs() const override { return m_position; }
    qint64 durationMs() const override { return loaded.isEmpty() ? 0 : m_duration; }
    void preload(const QString& path) override { preloaded << path; }

    Result<void> releaseFile(const QString& path) override
    {
        released << path;
        if (refuseRelease)
            return Error::make(ErrorCode::FileLocked, QStringLiteral("busy"));
        return {};
    }
    void fileReady(const QString& path) override { ready << path; }

    // ---- test controls
    bool isPlaying() const { return m_playing; }
    // Moves the playhead like a real backend would: `ms` in steps of at most `step`.
    void advance(qint64 ms, qint64 step = 1000)
    {
        while (ms > 0 && m_playing) {
            const qint64 d = std::min(ms, step);
            ms -= d;
            m_position = std::min(m_position + d, m_duration);
            emit positionChanged(m_position);
            if (m_position >= m_duration) {
                m_playing = false;
                emit mediaEnded();  // the service may start the next track; then the loop goes on with it
            }
        }
    }
    void finish()
    {
        m_position = m_duration;
        m_playing = false;
        emit mediaEnded();
    }
    void failLater(const QString& path) { emit errorOccurred(path, QStringLiteral("device lost")); }

    QHash<QString, qint64> durations;
    std::function<qint64(const QString&)> durationProvider;  // fallback: e.g. the catalog's durations
    qint64 defaultDuration = 180000;
    QSet<QString> failOnLoad;
    QStringList loads, preloaded, released, ready;
    std::optional<ReleasedPlayback> pendingRelease;  // set by a test: the player let go of its file for a rename
    std::optional<ReleasedPlayback> takeReleased() override { return std::exchange(pendingRelease, std::nullopt); }
    QList<qint64> seeks;
    QString loaded;
    double volume = -1;
    bool muted = false;
    bool refuseRelease = false;
    int playCalls = 0, pauseCalls = 0, stopCalls = 0;

private:
    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_playing = false;
};

class FakeCatalog final : public ITrackCatalog {
public:
    void add(qint64 id, qint64 durationMs = 180000, const QString& title = {})
    {
        PlayableTrack t;
        t.id = TrackId(id);
        t.absolutePath = pathOf(id);
        t.title = title.isEmpty() ? QStringLiteral("Track %1").arg(id) : title;
        t.artist = QStringLiteral("Artist");
        t.albumTitle = QStringLiteral("Album");
        t.durationMs = durationMs;
        m_tracks.insert(t.id, t);
    }
    void addRange(qint64 first, int count, qint64 durationMs = 180000)
    {
        for (int i = 0; i < count; ++i)
            add(first + i, durationMs);
    }
    void remove(qint64 id) { m_tracks.remove(TrackId(id)); }
    void moveTo(qint64 id, const QString& newPath)
    {
        if (auto it = m_tracks.find(TrackId(id)); it != m_tracks.end())
            it->absolutePath = newPath;
    }
    qint64 durationOfPath(const QString& path) const
    {
        for (const PlayableTrack& t : m_tracks)
            if (t.absolutePath == path)
                return t.durationMs;
        return 180000;
    }
    static QString pathOf(qint64 id) { return QStringLiteral("/music/t%1.mp3").arg(id); }
    std::optional<PlayableTrack> resolve(TrackId id) override
    {
        const auto it = m_tracks.constFind(id);
        return it == m_tracks.constEnd() ? std::nullopt : std::optional<PlayableTrack>(*it);
    }

private:
    QHash<TrackId, PlayableTrack> m_tracks;
};

class FakeStore final : public IPlaybackStore {
public:
    void recordPlay(TrackId id, qint64) override { plays[id.value]++; playOrder << id.value; }
    void touchRecent(ContainerKind kind, qint64 id, qint64) override
    {
        const QPair<int, qint64> key(static_cast<int>(kind), id);
        recents.removeAll(key);
        recents.prepend(key);
        while (recents.size() > 50)
            recents.removeLast();
        touches.append(key);
    }
    QJsonObject loadSession() override { return session; }
    void saveSession(const QJsonObject& s) override { session = s; ++saves; }

    QHash<qint64, int> plays;
    QList<qint64> playOrder;
    QList<QPair<int, qint64>> recents;  // newest first
    QList<QPair<int, qint64>> touches;  // every call
    QJsonObject session;
    int saves = 0;
};

class FakeRecommender final : public IRecommendationSource {
public:
    QList<TrackId> pick(const RecommendationRequest& request) override
    {
        requests << request;
        QList<TrackId> out;
        for (TrackId id : std::as_const(pool)) {
            if (out.size() >= request.count)
                break;
            if (!request.exclude.contains(id) && !out.contains(id))
                out.append(id);
        }
        return out;
    }
    QList<TrackId> pool;
    QList<RecommendationRequest> requests;
};

class FakeSettings final : public ISettings {
public:
    QString language() const override { return QStringLiteral("en"); }
    void setLanguage(const QString&) override {}
    QString userName() const override { return {}; }
    void setUserName(const QString&) override {}
    QString theme() const override { return QStringLiteral("dark"); }
    void setTheme(const QString&) override {}
    double volume() const override { return m_volume; }
    void setVolume(double v) override { m_volume = v; }
    bool muted() const override { return m_muted; }
    void setMuted(bool m) override { m_muted = m; }
    QString libraryPath() const override { return {}; }
    void setLibraryPath(const QString&) override {}
    WindowGeometry windowGeometry() const override { return {}; }
    void setWindowGeometry(const WindowGeometry&) override {}
    double m_volume = 0.5;
    bool m_muted = false;
};

class FakeControls final : public ISystemMediaControls {
    Q_OBJECT
public:
    using ISystemMediaControls::ISystemMediaControls;
    void setMetadata(const MediaMetadata& m) override { metadata = m; ++metadataCalls; }
    void setState(PlayState s) override { state = s; ++stateCalls; }
    void setEnabled(bool p, bool n, bool c) override { canPrev = p; canNext = n; canPlay = c; }
    MediaMetadata metadata;
    int metadataCalls = 0, stateCalls = 0;
    PlayState state = PlayState::Stopped;
    bool canPrev = false, canNext = false, canPlay = false;
};

class FakeThumbBar final : public IThumbBar {
    Q_OBJECT
public:
    using IThumbBar::IThumbBar;
    void setState(bool playing, bool p, bool n, bool c) override { isPlaying = playing; canPrev = p; canNext = n; canPlay = c; ++calls; }
    int calls = 0;
    bool isPlaying = false, canPrev = false, canNext = false, canPlay = false;
};

}  // namespace semplice::testing
