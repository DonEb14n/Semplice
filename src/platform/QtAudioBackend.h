#pragma once

#include "core/playback/PlaybackInterfaces.h"

#include <QMutex>
#include <optional>
#include <QThread>

#include <atomic>

namespace semplice::platform {

class AudioEngine;

// IAudioBackend on Qt Multimedia (SPEC 7.1). The ONLY place that uses QMediaPlayer / QAudioOutput.
// Gapless playback is not offered by Qt: preload() is accepted and ignored, the short gap between two
// tracks is accepted by the spec.
//
// The player lives on a thread of its own (`AudioEngine`): stopping or reopening a file can take a second
// inside Qt Multimedia, and the window must not wait for that. This object is the GUI thread's handle: commands
// are queued, position / duration are copies that the audio thread keeps up to date.
class QtAudioBackend final : public IAudioBackend {
    Q_OBJECT
public:
    explicit QtAudioBackend(QObject* parent = nullptr);
    ~QtAudioBackend() override;

    void load(const QString& absolutePath) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;
    void setVolume(double volume01) override;
    void setMuted(bool muted) override;
    qint64 positionMs() const override;
    qint64 durationMs() const override;

    // Tag writes, renames and deletes need the file to be closed here (Windows will not allow them otherwise). These two
    // may be called from any thread; they wait for the audio thread, never for the GUI thread.
    Result<void> releaseFile(const QString& absolutePath) override;
    void fileReady(const QString& absolutePath) override;
    std::optional<ReleasedPlayback> takeReleased() override;
    bool inUse(const QString& absolutePath) const override { return maybeAffectedBy(absolutePath); }

    // Thread-safe and cheap: could this path be the loaded file, or lie inside a folder that is about to change?
    bool maybeAffectedBy(const QString& other) const;

private:
    friend class AudioEngine;
    void setSharedPath(const QString& path);
    void setReleasedState(std::optional<ReleasedPlayback> state);

    QThread m_thread;
    AudioEngine* m_engine = nullptr;
    mutable QMutex m_sharedMutex;
    QString m_sharedPath;       // the file that is loaded (for other threads)
    std::optional<ReleasedPlayback> m_releasedState;  // set while the file is let go of (under m_sharedMutex)
    qint64 m_position = 0;      // last position reported by the audio thread
    qint64 m_duration = 0;
    qint64 m_seekTarget = -1;   // a seek that the audio thread has not confirmed yet
};

}  // namespace semplice::platform
