#pragma once

#include "core/playback/LibraryPlayback.h"

#include <QObject>
#include <QThread>

#include <memory>

namespace semplice {

// The playback store for the GUI thread. Writing (play counts, Recently Played, the saved queue) happens on a thread of
// its own with its own connection: while an album is being saved or deleted the engine thread holds the database for
// seconds, and a write from the GUI thread would wait for it (the music froze). Reading the saved session is done at
// start-up on the caller's connection.
class AsyncPlaybackStore final : public QObject, public IPlaybackStore {
    Q_OBJECT
public:
    AsyncPlaybackStore(const QString& dbPath, Database& readerConnection, QObject* parent = nullptr);
    ~AsyncPlaybackStore() override;

    void recordPlay(TrackId id, qint64 nowMs) override;
    void touchRecent(ContainerKind kind, qint64 id, qint64 nowMs) override;
    QJsonObject loadSession() override;
    void saveSession(const QJsonObject& session) override;

    // Waits until everything written so far is on disk (shutdown, tests).
    void flush();

signals:
    void recentWritten();  // Recently Played changed on disk: the Home page may read it

private:
    struct Writer;
    QThread m_thread;
    Writer* m_writer = nullptr;
    LibraryPlaybackStore m_reader;
};

}  // namespace semplice
