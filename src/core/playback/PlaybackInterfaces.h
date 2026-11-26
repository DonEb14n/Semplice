#pragma once

#include "core/playback/PlaybackTypes.h"
#include "core/tags/IFileReleaser.h"

#include <QJsonObject>
#include <QObject>

#include <optional>

namespace semplice {

// ---- audio ------------------------------------------------------------------------------------------
// The only door to sound (SPEC 7.1). Implementations: QtAudioBackend (platform), FakeAudioBackend (tests).
// Nothing outside the implementation may use QMediaPlayer / QAudioOutput.
// What the backend remembers when it let go of the file it was playing (see IFileReleaser).
struct ReleasedPlayback {
    qint64 positionMs = 0;
    bool playing = false;
};

class IAudioBackend : public QObject, public IFileReleaser {
    Q_OBJECT
public:
    using QObject::QObject;

    // Opens `absolutePath` (stopped, position 0). Failures are reported through errorOccurred, possibly later.
    virtual void load(const QString& absolutePath) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 positionMs) = 0;  // before the media is ready: remembered and applied once it is
    virtual void setVolume(double volume01) = 0;
    virtual void setMuted(bool muted) = 0;
    virtual qint64 positionMs() const = 0;
    virtual qint64 durationMs() const = 0;
    // Hint for gapless playback; a backend without support ignores it (a short gap is accepted).
    virtual void preload(const QString& absolutePath) { Q_UNUSED(absolutePath) }
    // The file was let go of for a rename or delete and could not be taken back at its old path: the caller loads the
    // track from its new place and continues from this position. Returns nothing when nothing is waiting.
    virtual std::optional<ReleasedPlayback> takeReleased() { return std::nullopt; }

signals:
    void positionChanged(qint64 positionMs);  // about every 250 ms while playing
    void durationChanged(qint64 durationMs);
    void mediaEnded();                         // the track played to its end by itself
    void errorOccurred(const QString& absolutePath, const QString& message);
};

// ---- library side -----------------------------------------------------------------------------------
struct PlayableTrack {
    TrackId id;
    QString absolutePath;
    QString title;
    QString artist;            // display text of the credits ("A, B")
    QString albumTitle;        // home album, empty for playlist-homed tracks
    qint64 durationMs = 0;
    QString coverPath;         // absolute path of the home album's (or playlist's) cover, may be empty
    bool hasEmbeddedCover = false;
    ContainerKind homeKind = ContainerKind::Album;  // where the file lives: the title opens this album / playlist
    qint64 homeId = 0;
    QList<qint64> artistIds;                        // credited artists in order (the artist line opens the first)
    QStringList artistNames;                        // the same artists as names (one link each in the player)
};

class ITrackCatalog {
public:
    virtual ~ITrackCatalog() = default;
    // nullopt: the track was deleted or its file is not there any more.
    virtual std::optional<PlayableTrack> resolve(TrackId id) = 0;
};

class IPlaybackStore {
public:
    virtual ~IPlaybackStore() = default;
    virtual void recordPlay(TrackId id, qint64 nowMs) = 0;                          // play_count++, last_played_at
    virtual void touchRecent(ContainerKind kind, qint64 id, qint64 nowMs) = 0;     // Recently Played
    virtual QJsonObject loadSession() = 0;                                          // empty object = none
    virtual void saveSession(const QJsonObject& session) = 0;
};

// ---- recommendations (implemented in M9) -------------------------------------------------------------
struct RecommendationRequest {
    QList<TrackId> seedTracks;  // everything that was just played from albums / playlists / carousels
    QList<TrackId> exclude;     // already queued or playing
    int count = 3;
};

class IRecommendationSource {
public:
    virtual ~IRecommendationSource() = default;
    // Up to `count` playable tracks, best first; empty when the pool has fewer than 2 playable tracks.
    virtual QList<TrackId> pick(const RecommendationRequest& request) = 0;
};

class NoRecommendations final : public IRecommendationSource {
public:
    QList<TrackId> pick(const RecommendationRequest&) override { return {}; }
};

// ---- Windows integration (implemented in platform/) ----------------------------------------------------
struct MediaMetadata {
    QString title;
    QString artist;
    QString album;
    QString coverPath;  // absolute, may be empty
    QByteArray coverData;  // the picture itself (file or embedded in the track), may be empty
};

class ISystemMediaControls : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setMetadata(const MediaMetadata& metadata) = 0;
    virtual void setState(PlayState state) = 0;
    virtual void setEnabled(bool canPrevious, bool canNext, bool canPlay) = 0;
signals:
    void playRequested();
    void pauseRequested();
    void toggleRequested();
    void nextRequested();
    void previousRequested();
};

class IThumbBar : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void setState(bool playing, bool canPrevious, bool canNext, bool canPlay) = 0;
signals:
    void previousClicked();
    void playPauseClicked();
    void nextClicked();
};

}  // namespace semplice
