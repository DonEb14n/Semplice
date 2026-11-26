// semplice_playtool: plays a REAL library through the real audio device, with the real Windows
// integration (media keys, volume flyout widget, taskbar thumbnail buttons), before the UI exists.
//
//   semplice_playtool <library> [--album NAME | --playlist NAME | --all] [--no-restore]
//
// A small window takes the keyboard: Space play/pause, Left/Right previous/next, Up/Down volume,
// M mute, S shuffle, R repeat (off -> all -> one), Q quit. Everything is printed to the console.
#include "core/playback/LibraryPlayback.h"
#include "core/playback/PlaybackService.h"
#include "core/services/LibrarySession.h"
#include "core/storage/ITrash.h"
#include "platform/QtAudioBackend.h"
#include "platform/WinSmtc.h"
#include "platform/WinThumbBar.h"
#include "platform/WindowsAtomicReplace.h"
#include "platform/WindowsCodepageConverter.h"
#include "platform/WindowsFileAttributes.h"

#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTextStream>
#include <QTimer>
#include <QWindow>

using namespace semplice;

namespace {

QTextStream out(stdout);

class KeyWindow final : public QWindow {
public:
    explicit KeyWindow(PlaybackService& service) : m_service(service)
    {
        setTitle(QStringLiteral("Semplice player test - Space, arrows, M, S, R, Q"));
        resize(520, 120);
    }

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        switch (e->key()) {
        case Qt::Key_Space: m_service.toggle(); break;
        case Qt::Key_Right: m_service.next(); break;
        case Qt::Key_Left: m_service.previous(); break;
        case Qt::Key_Up: m_service.setVolume(m_service.volume() + 0.1); break;
        case Qt::Key_Down: m_service.setVolume(m_service.volume() - 0.1); break;
        case Qt::Key_M: m_service.setMuted(!m_service.muted()); break;
        case Qt::Key_S: m_service.setShuffle(!m_service.shuffle()); break;
        case Qt::Key_R:
            m_service.setRepeat(m_service.repeat() == RepeatMode::Off ? RepeatMode::All
                                : m_service.repeat() == RepeatMode::All ? RepeatMode::One
                                                                        : RepeatMode::Off);
            break;
        case Qt::Key_Q: QGuiApplication::quit(); break;
        default: QWindow::keyPressEvent(e);
        }
    }

private:
    PlaybackService& m_service;
};

class MemorySettings final : public ISettings {
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

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        out << "usage: semplice_playtool <library> [--album NAME | --playlist NAME | --all] [--no-restore]\n";
        return 1;
    }
    const QString root = QDir::cleanPath(args.at(0));
    QString albumName, playlistName;
    bool all = false, restore = true;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--album") && i + 1 < args.size())
            albumName = args.at(++i);
        else if (args.at(i) == QLatin1String("--playlist") && i + 1 < args.size())
            playlistName = args.at(++i);
        else if (args.at(i) == QLatin1String("--all"))
            all = true;
        else if (args.at(i) == QLatin1String("--no-restore"))
            restore = false;
    }

    QtTrash trash;
    platform::WindowsAtomicReplace replace;
    platform::WindowsCodepageConverter codepages;
    SystemClock clock;
    platform::WindowsFileAttributes attributes;
    platform::QtAudioBackend backend;
    SessionDependencies deps{trash, replace, codepages, clock, attributes, &backend, nullptr};
    auto opened = LibrarySwitcher::select(root, deps);
    if (!opened.ok()) {
        out << "cannot open the library: " << opened.error().symbol() << " " << opened.error().devMessage << "\n";
        return 1;
    }
    LibrarySession& session = *opened.value();

    MemorySettings settings;
    LibraryTrackCatalog catalog(session.database(), session.layout().root);
    LibraryPlaybackStore store(session.database());
    PlaybackService service({backend, catalog, store, clock, nullptr, &settings, 0});

    KeyWindow window(service);
    window.show();
    platform::WinSmtc smtc;
    platform::WinThumbBar thumbs;
    out << "media controls attached: " << (smtc.attach(window.winId()) ? "yes" : "NO") << "\n";
    thumbs.attach(window.winId());
    SystemIntegration glue(service, &smtc, &thumbs);

    QObject::connect(&service, &PlaybackService::currentChanged, [&] {
        if (service.currentTrack())
            out << "now playing: " << service.currentTrack()->artist << " - " << service.currentTrack()->title << " ("
                << service.currentTrack()->albumTitle << ")\n";
        out.flush();
    });
    QObject::connect(&service, &PlaybackService::stateChanged, [&](PlayState s) {
        out << "state: " << (s == PlayState::Playing ? "playing" : s == PlayState::Paused ? "paused" : "stopped") << "\n";
        out.flush();
    });
    QObject::connect(&service, &PlaybackService::tracksSkipped, [](int n) { out << n << " track(s) could not be played and were skipped\n"; out.flush(); });
    QObject::connect(&backend, &IAudioBackend::errorOccurred, [](const QString& p, const QString& m) { out << "audio error: " << m << " (" << p << ")\n"; out.flush(); });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&] { service.persist(); });

    QTimer persistTimer;
    persistTimer.setInterval(5000);
    QObject::connect(&persistTimer, &QTimer::timeout, [&] { service.persist(); });
    persistTimer.start();

    if (restore)
        service.restore();
    AlbumRepo albums(session.database());
    PlaylistRepo playlists(session.database());
    PlayRequest request;
    request.afterEnd = AfterEnd::Recommend;
    auto addAlbum = [&](const Album& a) {
        QueueGroup g;
        g.origin = {OriginKind::Album, a.id.value, a.title};
        if (auto ids = albums.tracks(a.id); ids.ok())
            for (TrackId id : ids.value())
                g.tracks.append({id});
        request.groups.append(g);
    };
    if (all || !albumName.isEmpty()) {
        if (auto list = albums.all(); list.ok())
            for (const Album& a : list.value())
                if (!a.isUnsorted && (all || a.title.compare(albumName, Qt::CaseInsensitive) == 0))
                    addAlbum(a);
    } else if (!playlistName.isEmpty()) {
        if (auto p = playlists.findByName(playlistName); p.ok() && p.value()) {
            QueueGroup g;
            g.origin = {OriginKind::Playlist, p.value()->id.value, p.value()->name};
            if (auto ids = playlists.tracks(p.value()->id); ids.ok())
                for (TrackId id : ids.value())
                    g.tracks.append({id});
            request.groups.append(g);
            request.afterEnd = AfterEnd::Stop;
        }
    }
    if (!request.groups.isEmpty())
        out << (service.play(request) ? "playing\n" : "nothing could be played\n");
    else if (service.currentTrack())
        out << "restored the last session (paused): press Space\n";
    else
        out << "nothing to play: give --album NAME, --playlist NAME or --all\n";
    out.flush();
    return app.exec();
}
