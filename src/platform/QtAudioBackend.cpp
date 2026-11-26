#include "platform/QtAudioBackend.h"

#include "core/util/Logger.h"
#include "platform/SharedReadFile.h"

#include <QAudio>
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace semplice::platform {

namespace {
bool pathIsOrContains(const QString& folderOrFile, const QString& playing)
{
    if (playing.isEmpty())
        return false;
    const QString target = QFileInfo(folderOrFile).absoluteFilePath();
    return playing.compare(target, Qt::CaseInsensitive) == 0 || playing.startsWith(target + QLatin1Char('/'), Qt::CaseInsensitive);
}
}  // namespace

// Lives on the audio thread; everything that touches Qt Multimedia happens here.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QtAudioBackend* owner) : m_owner(owner) {}

    void init()
    {
        m_player = new QMediaPlayer(this);
        m_output = new QAudioOutput(this);
        m_timer = new QTimer(this);
        m_player->setAudioOutput(m_output);
        m_timer->setInterval(250);
        // Windows' default sound device changed (headphones plugged in, another speaker chosen): the music moves there at once.
        // Qt keeps the device it started with unless it is told.
        m_devices = new QMediaDevices(this);
        connect(m_devices, &QMediaDevices::audioOutputsChanged, this, [this] {
            const QAudioDevice wanted = QMediaDevices::defaultAudioOutput();
            if (!wanted.isNull() && m_output->device().id() != wanted.id()) {
                SLOG_INFO(QStringLiteral("the sound output changed: now %1").arg(wanted.description()));
                m_output->setDevice(wanted);
            }
        });
        connect(m_timer, &QTimer::timeout, this, [this] {
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                emit position(m_player->position());
        });
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &AudioEngine::onStatus);
        connect(m_player, &QMediaPlayer::errorOccurred, this, &AudioEngine::onError);
        connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 d) { emit duration(d); });
    }
    void shutdown()
    {
        m_timer->stop();
        if (m_device)
            m_device->cancel();
        m_player->stop();
        m_player->setSource(QUrl());
        m_device.reset();  // cancelled above; nothing reads it any more
        delete m_player;  // the player and its output end on the thread that made them
        m_player = nullptr;
        delete m_output;
        m_output = nullptr;
    }

    void load(const QString& path)
    {
        QElapsedTimer t;
        t.start();
        // The player service asks for the file of the track that was just moved by a rename: the very same file at a new place.
        // Nothing is reopened, the device only gets the new path and the music goes on.
        if (m_released && m_device && m_device->isSuspended() && m_device->sameFileAs(path) && m_device->resume(path)) {
            m_path = path;
            m_owner->setSharedPath(path);
            m_released = false;
            m_owner->setReleasedState(std::nullopt);
            m_skipSeek = true;
            m_skipClock.start();  // the service now seeks to where the music was: it still is
            SLOG_INFO(QStringLiteral("player: the music goes on from the new place (at %1 ms)").arg(m_player->position()));
            QTimer::singleShot(1500, this, [this] { SLOG_INFO(QStringLiteral("player: 1.5 s later at %1 ms, state %2").arg(m_player->position()).arg(int(m_player->playbackState()))); });
            emit duration(m_player->duration());
            emit position(m_player->position());
            return;
        }
        m_timer->stop();
        // a reader that waits for a resume, or an open that is not finished, must not hold up the next source
        if (m_device && (m_device->isSuspended() || m_player->mediaStatus() == QMediaPlayer::LoadingMedia)) {
            m_device->cancel();
            m_expectError = true;  // the old media fails because of that: not the new one's fault
        }
        if (m_player->playbackState() != QMediaPlayer::StoppedState)  // stopping an idle player can cost a second
            m_player->stop();
        const qint64 stopped = t.elapsed();
        m_path = path;
        m_owner->setSharedPath(path);
        m_pendingSeek = -1;
        m_released = false;
        m_skipSeek = false;
        open(path);
        if (t.elapsed() > 50)
            SLOG_WARN(QStringLiteral("the player took %1 ms to open a file (stop %2 ms, on the audio thread)").arg(t.elapsed()).arg(stopped));
    }
    void play()
    {
        QElapsedTimer t;
        t.start();
        m_player->play();
        m_timer->start();
        if (t.elapsed() > 50)
            SLOG_WARN(QStringLiteral("play() took %1 ms (on the audio thread)").arg(t.elapsed()));
    }
    void pause()
    {
        m_player->pause();
        m_timer->stop();
    }
    void stop()
    {
        m_timer->stop();
        m_player->stop();
        m_pendingSeek = -1;
    }
    void seek(qint64 ms)
    {
        if (m_skipSeek) {
            m_skipSeek = false;  // right after a rename: the music is where it should be
            if (m_skipClock.isValid() && m_skipClock.elapsed() < 2000)
                return;
        }
        const auto status = m_player->mediaStatus();
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::BufferingMedia ||
            status == QMediaPlayer::EndOfMedia)
            m_player->setPosition(ms);
        else
            m_pendingSeek = ms;  // applied once the file is open
    }
    void setVolume(double volume01)
    {
        // The service hands over a "slider" value; the output wants a linear amplitude.
        m_output->setVolume(static_cast<float>(QAudio::convertVolume(std::clamp(volume01, 0.0, 1.0), QAudio::LogarithmicVolumeScale,
                                                                    QAudio::LinearVolumeScale)));
    }
    void setMuted(bool muted) { m_output->setMuted(muted); }

    // ---- let go of / take back the file (called blocking from another thread)
    void release(const QString& path)
    {
        if (m_path.isEmpty() || !m_device || !pathIsOrContains(path, QFileInfo(m_path).absoluteFilePath()) || m_released)
            return;
        m_resumePosition = m_player->position();
        m_resumePlaying = m_player->playbackState() == QMediaPlayer::PlayingState;
        m_released = true;
        m_owner->setReleasedState(ReleasedPlayback{m_resumePosition, m_resumePlaying});
        m_device->suspend();  // the handle is closed in an instant, the player just waits for data; it is never stopped
        SLOG_INFO(QStringLiteral("player: the file is let go of while the music waits (at %1 ms)").arg(m_resumePosition));
    }
    void ready(const QString& path)
    {
        if (!m_released || m_path.isEmpty() || !m_device || !pathIsOrContains(path, QFileInfo(m_path).absoluteFilePath()))
            return;
        if (!QFileInfo::exists(m_path))
            return;  // it was moved or deleted: the player service finds the new place (takeReleased) and load() resumes
        if (!m_device->resume(m_path))
            return;
        m_released = false;
        m_owner->setReleasedState(std::nullopt);
    }

signals:
    void position(qint64 ms);
    void duration(qint64 ms);
    void ended();
    void error(const QString& path, const QString& text);

private:
    // The player reads through our own device, so that Windows may rename the file while it plays (see SharedReadFile).
    void open(const QString& path)
    {
        auto fresh = path.isEmpty() ? nullptr : std::make_unique<SharedReadFile>(path);
        const bool opened = fresh && fresh->open();
        if (opened)
            m_player->setSourceDevice(fresh.get(), QUrl::fromLocalFile(path));
        else
            m_player->setSource(path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path));
        if (fresh && !fresh->isOpen())
            fresh.reset();
        if (m_device)
            m_device.release()->deleteLater();  // the old device goes only now: the player no longer uses it
        m_device.reset(fresh.release());
    }
    void onStatus(QMediaPlayer::MediaStatus status)
    {
        switch (status) {
        case QMediaPlayer::LoadedMedia:
        case QMediaPlayer::BufferedMedia:
            m_expectError = false;
            if (m_pendingSeek >= 0) {
                m_player->setPosition(m_pendingSeek);
                m_pendingSeek = -1;
            }
            break;
        case QMediaPlayer::EndOfMedia:
            m_timer->stop();
            emit ended();
            break;
        default: break;
        }
    }
    void onError(QMediaPlayer::Error err, const QString& text)
    {
        if (err == QMediaPlayer::NoError || m_released)
            return;
        if (m_expectError) {
            m_expectError = false;  // the old media failed because its reader was cancelled
            return;
        }
        m_timer->stop();
        emit error(m_path, text);
    }

    QtAudioBackend* m_owner;
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_output = nullptr;
    QMediaDevices* m_devices = nullptr;
    QTimer* m_timer = nullptr;
    QString m_path;
    qint64 m_pendingSeek = -1;
    bool m_released = false;
    bool m_skipSeek = false;
    bool m_expectError = false;
    QElapsedTimer m_skipClock;
    std::unique_ptr<SharedReadFile> m_device;
    qint64 m_resumePosition = 0;
    bool m_resumePlaying = false;
};

// ------------------------------------------------------------------------------------------ the GUI-thread handle

QtAudioBackend::QtAudioBackend(QObject* parent) : IAudioBackend(parent)
{
    m_engine = new AudioEngine(this);
    m_engine->moveToThread(&m_thread);
    m_thread.setObjectName(QStringLiteral("semplice-audio"));
    m_thread.start(QThread::TimeCriticalPriority);  // the sound must not wait for the window
    QMetaObject::invokeMethod(m_engine, [e = m_engine] { e->init(); }, Qt::BlockingQueuedConnection);
    connect(m_engine, &AudioEngine::position, this, [this](qint64 ms) {
        m_position = ms;
        if (m_seekTarget >= 0 && std::abs(ms - m_seekTarget) < 1500)
            m_seekTarget = -1;
        emit positionChanged(ms);
    });
    connect(m_engine, &AudioEngine::duration, this, [this](qint64 ms) {
        m_duration = ms;
        emit durationChanged(ms);
    });
    connect(m_engine, &AudioEngine::ended, this, &QtAudioBackend::mediaEnded);
    connect(m_engine, &AudioEngine::error, this, &QtAudioBackend::errorOccurred);
}

QtAudioBackend::~QtAudioBackend()
{
    QMetaObject::invokeMethod(m_engine, [e = m_engine] { e->shutdown(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    delete m_engine;
}

void QtAudioBackend::setSharedPath(const QString& path)
{
    QMutexLocker lock(&m_sharedMutex);
    m_sharedPath = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

bool QtAudioBackend::maybeAffectedBy(const QString& other) const
{
    QString playing;
    {
        QMutexLocker lock(&m_sharedMutex);
        playing = m_sharedPath;
    }
    return pathIsOrContains(other, playing);
}

void QtAudioBackend::load(const QString& absolutePath)
{
    m_position = 0;
    m_duration = 0;
    m_seekTarget = -1;
    setSharedPath(absolutePath);
    QMetaObject::invokeMethod(m_engine, [e = m_engine, absolutePath] { e->load(absolutePath); }, Qt::QueuedConnection);
}

void QtAudioBackend::play()
{
    QMetaObject::invokeMethod(m_engine, [e = m_engine] { e->play(); }, Qt::QueuedConnection);
}

void QtAudioBackend::pause()
{
    QMetaObject::invokeMethod(m_engine, [e = m_engine] { e->pause(); }, Qt::QueuedConnection);
}

void QtAudioBackend::stop()
{
    m_seekTarget = -1;
    QMetaObject::invokeMethod(m_engine, [e = m_engine] { e->stop(); }, Qt::QueuedConnection);
}

void QtAudioBackend::seek(qint64 positionMs)
{
    m_seekTarget = positionMs;
    m_position = positionMs;
    QMetaObject::invokeMethod(m_engine, [e = m_engine, positionMs] { e->seek(positionMs); }, Qt::QueuedConnection);
}

void QtAudioBackend::setVolume(double volume01)
{
    QMetaObject::invokeMethod(m_engine, [e = m_engine, volume01] { e->setVolume(volume01); }, Qt::QueuedConnection);
}

void QtAudioBackend::setMuted(bool muted)
{
    QMetaObject::invokeMethod(m_engine, [e = m_engine, muted] { e->setMuted(muted); }, Qt::QueuedConnection);
}

qint64 QtAudioBackend::positionMs() const
{
    return m_seekTarget >= 0 ? m_seekTarget : m_position;
}

qint64 QtAudioBackend::durationMs() const
{
    return m_duration;
}

Result<void> QtAudioBackend::releaseFile(const QString& absolutePath)
{
    if (!maybeAffectedBy(absolutePath))
        return {};
    QMetaObject::invokeMethod(m_engine, [e = m_engine, absolutePath] { e->release(absolutePath); }, Qt::BlockingQueuedConnection);
    return {};
}

void QtAudioBackend::fileReady(const QString& absolutePath)
{
    if (!maybeAffectedBy(absolutePath))
        return;
    QMetaObject::invokeMethod(m_engine, [e = m_engine, absolutePath] { e->ready(absolutePath); }, Qt::BlockingQueuedConnection);
}

void QtAudioBackend::setReleasedState(std::optional<ReleasedPlayback> state)
{
    QMutexLocker lock(&m_sharedMutex);
    m_releasedState = state;
}

std::optional<ReleasedPlayback> QtAudioBackend::takeReleased()
{
    std::optional<ReleasedPlayback> result;
    {
        QMutexLocker lock(&m_sharedMutex);
        result = std::exchange(m_releasedState, std::nullopt);
    }
    return result;  // never waits for the audio thread (the window must not freeze)
}

}  // namespace semplice::platform

#include "QtAudioBackend.moc"
