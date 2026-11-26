#pragma once

#include "core/playback/PlayQueue.h"
#include "core/playback/PlaybackInterfaces.h"
#include "core/services/ISettings.h"
#include "core/util/Clock.h"

#include <QObject>
#include <QSet>

namespace semplice {

// The playback state machine of SPEC 7 on top of the audio backend. Single-threaded: lives in the
// thread of the backend (the UI thread; nothing here is slow).
class PlaybackService : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 kPreviousRestartMs = 5000;     // Previous restarts the track after this much
    static constexpr qint64 kStatsMaxMs = 30000;           // min(30 s, 50 %) of the track
    static constexpr qint64 kMaxPositionStepMs = 2000;     // bigger jumps are seeks, they do not count as listening
    static constexpr int kLookAhead = 3;                   // recommended tracks kept ready ("Up next")

    struct Dependencies {
        IAudioBackend& backend;
        ITrackCatalog& catalog;
        IPlaybackStore& store;
        const IClock& clock;
        IRecommendationSource* recommender = nullptr;  // nullptr = none
        ISettings* settings = nullptr;                 // volume and mute are kept here
        quint64 randomSeed = 0;                        // 0 = random
    };
    explicit PlaybackService(const Dependencies& deps, QObject* parent = nullptr);

    // ---- start
    // false when nothing playable was in the request.
    bool play(const PlayRequest& request);
    // Brings back the saved session, paused, without the tracks that no longer exist.
    void restore();
    // Saves the session (the UI calls it every few seconds and at exit).
    void persist();

    // ---- transport
    void play();
    void pause();
    void toggle();
    void stop();
    void next();
    void previous();
    // Straight to the previous track in the history (clicking it in the side panel): no 5-second rule.
    void previousTrack();
    bool playAt(int group, int index);  // click in the queue panels
    void seek(qint64 positionMs);

    bool canPrevious() const;
    bool canNext() const;

    // ---- modes
    void setShuffle(bool on);
    void setRepeat(RepeatMode mode);
    void setVolume(double volume01);
    void setMuted(bool muted);
    bool shuffle() const { return m_queue.shuffle(); }
    RepeatMode repeat() const { return m_queue.repeat(); }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }

    // ---- state
    PlayState state() const { return m_state; }
    const std::optional<PlayableTrack>& currentTrack() const { return m_current; }
    std::optional<QueueOrigin> currentOrigin() const;
    qint64 positionMs() const;
    qint64 durationMs() const;
    const PlayQueue& queue() const { return m_queue; }

    // Tracks that were deleted / may have lost their file: removed from the queue (the current one is skipped).
    void revalidate(const QSet<TrackId>& touched);

signals:
    void stateChanged(semplice::PlayState state);
    void currentChanged();                       // another track (or none)
    void queueChanged();
    void modesChanged();                         // shuffle / repeat
    void volumeChanged();
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void tracksSkipped(int count);               // ONE banner per batch of unplayable tracks
    void recentlyPlayedChanged();

private:
    // loading
    void loadCurrent(bool autoplay);
    bool handleStep(PlayQueue::Step step);       // false = stopped
    bool startRecommendations();
    void ensureLookahead();
    QList<TrackId> seedTracks() const;
    void enterStopped();
    void announce();

    // backend events
    void onPosition(qint64 positionMs);
    void onDuration(qint64 durationMs);
    void onEnded();
    void onError(const QString& path, const QString& message);

    // statistics
    void resetListening();
    void evaluateListening();
    void registerRecent(const QueueTrack& entry);

    void setState(PlayState state);
    void preloadNext();
    void applyVolume();
    void saveSession();
    qint64 nowMs() const { return m_clock.now().toMSecsSinceEpoch(); }

    IAudioBackend& m_backend;
    ITrackCatalog& m_catalog;
    IPlaybackStore& m_store;
    const IClock& m_clock;
    IRecommendationSource* m_recommender;
    NoRecommendations m_none;
    ISettings* m_settings;

    PlayQueue m_queue;
    PlayState m_state = PlayState::Stopped;
    std::optional<PlayableTrack> m_current;
    double m_volume = 0.5;
    bool m_muted = false;

    // load bookkeeping
    bool m_inLoad = false;
    bool m_loadFailed = false;
    QString m_loadingPath;
    int m_skipped = 0;

    // listening statistics of the track that is playing
    qint64 m_listenedMs = 0;
    qint64 m_lastPos = 0;
    bool m_counted = false;
    int m_visitGroup = -1;
    QSet<qint64> m_registeredOrigins;  // origin keys already registered in this visit of the group
};

}  // namespace semplice
