#pragma once

#include "core/db/Database.h"
#include "core/playback/PlaybackInterfaces.h"

namespace semplice {

// ITrackCatalog and IPlaybackStore on top of one open library.
class LibraryTrackCatalog final : public ITrackCatalog {
public:
    LibraryTrackCatalog(Database& db, QString libraryRoot) : m_db(db), m_root(std::move(libraryRoot)) {}
    std::optional<PlayableTrack> resolve(TrackId id) override;

private:
    Database& m_db;
    QString m_root;
};

class LibraryPlaybackStore final : public IPlaybackStore {
public:
    explicit LibraryPlaybackStore(Database& db) : m_db(db) {}
    void recordPlay(TrackId id, qint64 nowMs) override;
    void touchRecent(ContainerKind kind, qint64 id, qint64 nowMs) override;
    QJsonObject loadSession() override;
    void saveSession(const QJsonObject& session) override;

    static constexpr const char* kSessionKey = "session";

private:
    Database& m_db;
};

// Keeps the system controls (SMTC flyout, media keys, taskbar thumbnail buttons) and the service in step.
class PlaybackService;
class SystemIntegration : public QObject {
    Q_OBJECT
public:
    // Either control may be null (not available on this platform / not created yet).
    SystemIntegration(PlaybackService& service, ISystemMediaControls* controls, IThumbBar* thumbBar, QObject* parent = nullptr);

private:
    void refresh();
    QByteArray coverDataOf(const PlayableTrack& track);
    PlaybackService& m_service;
    ISystemMediaControls* m_controls;
    IThumbBar* m_thumbBar;
    qint64 m_coverTrack = 0;     // the track whose embedded picture is cached below
    QByteArray m_cover;
    // what was sent to Windows last time: a save announces the same track again, and Windows is not told twice
    bool m_sent = false;
    QString m_sentTitle, m_sentArtist, m_sentAlbum, m_sentCoverPath;
    qint64 m_sentTrack = 0;
    PlayState m_sentState = PlayState::Stopped;
    bool m_sentPrev = false, m_sentNext = false, m_sentHasTrack = false;
};

}  // namespace semplice
