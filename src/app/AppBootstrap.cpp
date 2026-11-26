#include "app/AppBootstrap.h"

#include "core/services/SettingsService.h"
#include "core/util/Logger.h"
#include "platform/FramelessAnimation.h"
#include "platform/CrashHandler.h"
#include "platform/SingleInstance.h"
#include "platform/StallSampler.h"
#include "platform/StartMenuShortcut.h"
#include "ui/AppController.h"
#include "ui/ShellVm.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QIcon>
#include <QQuickItem>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <numeric>
#include <QTranslator>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace semplice {

namespace {

QIcon roundIcon()
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256})
        icon.addFile(QStringLiteral(":/icons/app/app-%1.png").arg(size));
    return icon;
}

QString logDirectory()
{
    // %LOCALAPPDATA%\Semplice\logs on Windows
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("Semplice/logs"));
}

void setupFonts(const QString& language)
{
    for (const char* file : {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-Bold.ttf"}) {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + QLatin1String(file));
        if (id < 0)
            SLOG_WARN(QStringLiteral("Could not load font %1").arg(QLatin1String(file)));
    }

    // Inter has no CJK glyphs. Japanese/Chinese UI prefers Segoe UI (UI doc p.52). The big CJK fonts are named only for the
    // languages that need them: every name in this list may have to be opened by the font engine for every text, which made
    // the start-up take seconds on a slower laptop. Track names in other scripts still get their glyphs from Windows' own
    // font fallback.
    QStringList families{QStringLiteral("Inter"), QStringLiteral("Segoe UI")};
    if (language == QLatin1String("ja"))
        families << QStringLiteral("Yu Gothic UI");
    else if (language == QLatin1String("zh_CN"))
        families << QStringLiteral("Microsoft YaHei UI");
    if (language == QLatin1String("ja") || language == QLatin1String("zh_CN"))
        families.move(1, 0);

    QFont font = QGuiApplication::font();
    font.setFamilies(families);
    QGuiApplication::setFont(font);
}

}  // namespace

// Scrolls the visible library list one page per frame and reports the frame times (acceptance: 60 fps with 50k items).
void runScrollBenchmark(QQuickWindow* window, QGuiApplication& app)
{
    const QString target = qEnvironmentVariable("SEMPLICE_BENCH_VIEW", QStringLiteral("libraryView"));
    QQuickItem* view = window->findChild<QQuickItem*>(target, Qt::FindChildrenRecursively);
    if (!view) {
        SLOG_ERROR(QStringLiteral("bench-scroll: no library list found"));
        app.exit(2);
        return;
    }
    auto times = std::make_shared<QList<qint64>>();
    auto timer = std::make_shared<QElapsedTimer>();
    auto frames = std::make_shared<int>(0);
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [=, &app] {
        if (timer->isValid())
            times->append(timer->nsecsElapsed());
        timer->restart();
        const double step = view->height() * 0.25;
        const double contentHeight = view->property("contentHeight").toDouble();
        double y = view->property("contentY").toDouble() + step;
        if (y > contentHeight - view->height() || ++*frames >= 600) {
            std::sort(times->begin(), times->end());
            const qint64 total = std::accumulate(times->begin(), times->end(), qint64(0));
            const double avgMs = times->isEmpty() ? 0 : total / 1e6 / times->size();
            const double p95Ms = times->isEmpty() ? 0 : times->at(int(times->size() * 0.95)) / 1e6;
            const double maxMs = times->isEmpty() ? 0 : times->last() / 1e6;
            SLOG_INFO(QStringLiteral("bench-scroll: %1 frames, avg %2 ms, p95 %3 ms, max %4 ms, rows %5")
                          .arg(times->size()).arg(avgMs, 0, 'f', 2).arg(p95Ms, 0, 'f', 2).arg(maxMs, 0, 'f', 2).arg(view->property("count").toInt()));
            app.quit();
            return;
        }
        view->setProperty("contentY", y);
        window->update();
    });
    window->update();
}

int runApplication(QGuiApplication& app)
{
    QElapsedTimer startClock;
    startClock.start();  // start-up timings go to the log (PERF_REPORT)
    app.setOrganizationName(QStringLiteral("Semplice"));
    app.setApplicationName(QStringLiteral("Semplice"));
#ifdef Q_OS_WIN
    // The identity Windows shows for the app in the volume panel and on the taskbar; the installer gives its shortcut the same id.
    SetCurrentProcessExplicitAppUserModelID(L"Semplice.Semplice");
#endif
    app.setWindowIcon(roundIcon());
    app.setApplicationVersion(QStringLiteral(SEMPLICE_VERSION));

    QCommandLineParser parser;
    QCommandLineOption grabOption(QStringLiteral("grab"),
                                  QStringLiteral("Save a screenshot of the window to <file> and quit (testing)."),
                                  QStringLiteral("file"));
    parser.addOption(grabOption);
    // Testing aids: start playing "<kind>:<id>" (PlayKind) as soon as the library is open; pick the library tab.
    QCommandLineOption devPlayOption(QStringLiteral("dev-play"), QStringLiteral("Play <kind>:<id> after start-up (testing)."), QStringLiteral("kind:id"));
    QCommandLineOption devTabOption(QStringLiteral("dev-tab"), QStringLiteral("Select library tab 0..2 (testing)."), QStringLiteral("n"));
    QCommandLineOption devDelayOption(QStringLiteral("grab-delay"), QStringLiteral("Milliseconds to wait before --grab."), QStringLiteral("ms"), QStringLiteral("1200"));
    QCommandLineOption benchScrollOption(QStringLiteral("bench-scroll"), QStringLiteral("Scroll the library list and report frame times (testing)."));
    parser.addOption(benchScrollOption);
    QCommandLineOption devOpenOption(QStringLiteral("dev-open"), QStringLiteral("Open page <kind>[:<id>[:<text>]] after start-up (testing)."), QStringLiteral("page"));
    QCommandLineOption devDropOption(QStringLiteral("dev-drop"), QStringLiteral("Show the drop-zone look (testing)."));
    parser.addOption(devOpenOption);
    QCommandLineOption devEditTrackOption(QStringLiteral("dev-edit-track"), QStringLiteral("Turn this track row into its editor (testing)."), QStringLiteral("id"));
    parser.addOption(devEditTrackOption);
    QCommandLineOption devSaveAlbumOption(QStringLiteral("dev-save-album"), QStringLiteral("Edit this album and save it with another year after start-up (testing)."), QStringLiteral("id"));
    parser.addOption(devSaveAlbumOption);
    QCommandLineOption devAddOption(QStringLiteral("dev-add"), QStringLiteral("Drop this file or folder on the window after start-up (testing)."), QStringLiteral("path"));
    parser.addOption(devAddOption);
    parser.addOption(devDropOption);
    QCommandLineOption devPopupOption(QStringLiteral("dev-popup"), QStringLiteral("Open the \"search\" or \"queue\" popup (testing)."), QStringLiteral("name"));
    QCommandLineOption devSearchOption(QStringLiteral("dev-search"), QStringLiteral("Type this into the search field (testing)."), QStringLiteral("text"));
    parser.addOption(devPopupOption);
    parser.addOption(devSearchOption);
    parser.addOption(devPlayOption);
    parser.addOption(devTabOption);
    parser.addOption(devDelayOption);
    parser.process(app);

    // 1. Logging and crash reports come first so everything after is recorded.
    Logger logger;
    if (logger.init(logDirectory())) {
        Logger::setGlobal(&logger);
        Logger::installQtMessageHandler();
    }
    platform::installCrashHandler(logDirectory(), app.applicationVersion());
    SLOG_INFO(QStringLiteral("Semplice %1 starting").arg(app.applicationVersion()));
    platform::startStallSampler();
    // watchdog of the GUI thread: any pause longer than 120 ms is logged (a freeze is then visible in the log with its length)
    {
        auto* beat = new QTimer(&app);
        auto* last = new QElapsedTimer;
        last->start();
        beat->setInterval(20);
        QObject::connect(beat, &QTimer::timeout, beat, [last] { last->restart(); });
        beat->start();
    }
    // the same watch without timers: a helper thread posts events and the GUI thread says how late it handled them
    {
        auto* posted = new std::atomic<qint64>(0);
        auto* clock = new QElapsedTimer;
        clock->start();
        QThread* watchdog = QThread::create([&app, posted, clock] {
            while (!QThread::currentThread()->isInterruptionRequested()) {
                QThread::msleep(20);
                if (posted->load() != 0)
                    continue;  // the last one was not handled yet
                const qint64 sentAt = clock->elapsed();
                posted->store(sentAt);
                QMetaObject::invokeMethod(&app, [posted, clock, sentAt] {
                    const qint64 late = clock->elapsed() - sentAt;
                    posted->store(0);
                    if (late > 120)
                        SLOG_WARN(QStringLiteral("an event waited %1 ms for the GUI thread").arg(late));
                }, Qt::QueuedConnection);
                // while the event waits: where is the GUI thread? (only with SEMPLICE_STALL_TRACE=1: reading a stack stops the GUI thread for a moment)
                for (int i = 0; i < 400 && posted->load() == sentAt && !QThread::currentThread()->isInterruptionRequested(); ++i) {
                    QThread::msleep(20);
                    if (i % 5 == 4 && posted->load() == sentAt && clock->elapsed() - sentAt > 100) {
                        const QString stack = platform::guiStackNow();
                        if (!stack.isEmpty())
                            SLOG_WARN(QStringLiteral("GUI thread busy for %1 ms, now at:%2").arg(clock->elapsed() - sentAt).arg(stack));
                    }
                }
            }
        });
        watchdog->setObjectName(QStringLiteral("semplice-watchdog"));
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [watchdog] {
            watchdog->requestInterruption();
            watchdog->wait();
            delete watchdog;
        });
        watchdog->start();
    }
#ifdef Q_OS_WIN
    {
        const bool shortcut = platform::ensureStartMenuShortcut(QCoreApplication::applicationFilePath(), QStringLiteral("Semplice.Semplice"), QStringLiteral("Semplice"));
        SLOG_INFO(QStringLiteral("start menu shortcut for the media panel: %1 (%2)").arg(shortcut ? QStringLiteral("ok") : QStringLiteral("FAILED"), QCoreApplication::applicationFilePath()));
    }
#endif

    // 2. Single instance: a second launch activates the first and exits.
    platform::SingleInstance instance(QStringLiteral("Semplice-activate"));
    if (!instance.acquire()) {
        SLOG_INFO(QStringLiteral("Another instance is running; exiting"));
        return 0;
    }

    // 3. Settings, language and fonts (before any QML is created).
    SettingsService settings(SettingsService::defaultPath());
    settings.writeMissingDefaults();
    const QString language = settings.language();
    QTranslator translator;
    if (language != QLatin1String("en")) {
        if (translator.load(QStringLiteral(":/i18n/app_%1.qm").arg(language)))
            app.installTranslator(&translator);
        else
            SLOG_WARN(QStringLiteral("No translation found for %1").arg(language));
    }
    setupFonts(language);

    // 4. View-models and QML.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    ShellVm shell(settings);
    AppController controller(settings, shell);  // before the engine: QML may still use it while the engine shuts down
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("cover"), controller.takeCoverProvider());
    engine.setInitialProperties({{QStringLiteral("shell"), QVariant::fromValue(&shell)},
                                 {QStringLiteral("app"), QVariant::fromValue(&controller)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Semplice", "Main");
    if (engine.rootObjects().isEmpty()) {
        SLOG_ERROR(QStringLiteral("Main.qml failed to load"));
        return 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (!window) {
        SLOG_ERROR(QStringLiteral("Main.qml root is not a window"));
        return 1;
    }
    shell.attach(window);
    static platform::FramelessAnimation framelessAnimation;
    framelessAnimation.install(window->winId());   // the minimise / restore animation of Windows for the frameless window
    controller.attachWindow(window);
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [startClock, window]() mutable {
        SLOG_INFO(QStringLiteral("perf: first frame after %1 ms").arg(startClock.elapsed()));
        QObject::disconnect(window, &QQuickWindow::frameSwapped, window, nullptr);
    });
    QObject::connect(&controller, &AppController::startupChanged, &controller, [startClock, &controller] {
        if (controller.startup() == AppController::Ready)
            SLOG_INFO(QStringLiteral("perf: library ready after %1 ms").arg(startClock.elapsed()));
    });
    QObject::connect(static_cast<HomeVm*>(controller.home()), &HomeVm::changed, &controller, [startClock, &controller] {
        static bool done = false;
        if (!done && controller.startup() == AppController::Ready) {
            done = true;
            SLOG_INFO(QStringLiteral("perf: home content after %1 ms").arg(startClock.elapsed()));
        }
    });
    if (parser.isSet(devTabOption))
        static_cast<LibraryVm*>(controller.library())->setTab(parser.value(devTabOption).toInt());
    if (parser.isSet(devPlayOption)) {
        const QStringList parts = parser.value(devPlayOption).split(QLatin1Char(':'));
        QObject::connect(&controller, &AppController::startupChanged, &controller, [&controller, parts] {
            if (controller.startup() == AppController::Ready && parts.size() == 2)
                controller.play(parts.at(0).toInt(), parts.at(1).toLongLong());
            if (controller.startup() == AppController::Ready && qEnvironmentVariableIsSet("SEMPLICE_DEV_REPLAY"))  // a second start while playing
                QTimer::singleShot(3000, &controller, [&controller, parts] { controller.play(parts.at(0).toInt(), parts.at(1).toLongLong()); });
        });
    }
    if (parser.isSet(devAddOption)) {
        const QString path = parser.value(devAddOption);
        QObject::connect(&controller, &AppController::startupChanged, &controller, [&controller, path] {
            if (controller.startup() == AppController::Ready)
                QTimer::singleShot(800, &controller, [&controller, path] { controller.dropFiles({QUrl::fromLocalFile(path)}); });
        });
    }
    if (parser.isSet(devSaveAlbumOption)) {
        const qint64 albumId = parser.value(devSaveAlbumOption).toLongLong();
        QObject::connect(&controller, &AppController::startupChanged, &controller, [&controller, albumId] {
            if (controller.startup() == AppController::Ready)
                QTimer::singleShot(4000, &controller, [&controller, albumId] { controller.devSaveAlbum(albumId); });
        });
    }
    if (parser.isSet(devEditTrackOption)) {
        const qint64 trackId = parser.value(devEditTrackOption).toLongLong();
        QObject::connect(&controller, &AppController::startupChanged, &controller, [&controller, trackId] {
            if (controller.startup() == AppController::Ready)
                QTimer::singleShot(1500, &controller, [&controller, trackId] { controller.editTrack(trackId); });
        });
    }
    if (parser.isSet(devDropOption))
        controller.setDevDropping(true);
    if (parser.isSet(devOpenOption)) {
        const QStringList parts = parser.value(devOpenOption).split(QLatin1Char(':'));
        QObject::connect(&controller, &AppController::startupChanged, &controller, [&controller, parts] {
            if (controller.startup() == AppController::Ready)
                QTimer::singleShot(600, &controller, [&controller, parts] {
                    controller.open(parts.at(0).toInt(), parts.value(1).toLongLong(), parts.mid(2).join(QLatin1Char(':')));
                });
        });
    }
    if (parser.isSet(devSearchOption))
        static_cast<SearchVm*>(controller.search())->setText(parser.value(devSearchOption));
    if (parser.isSet(devPopupOption)) {
        QObject::connect(&controller, &AppController::startupChanged, &controller, [&controller, name = parser.value(devPopupOption)] {
            if (controller.startup() == AppController::Ready)
                QTimer::singleShot(800, &controller, [&controller, name] { controller.setPopup(name); });
        });
    }
    if (parser.isSet(benchScrollOption)) {
        QObject::connect(&controller, &AppController::startupChanged, &controller, [&controller, window, &app] {
            if (controller.startup() != AppController::Ready)
                return;
            QTimer::singleShot(2500, &app, [window, &app] { runScrollBenchmark(window, app); });
        });
    }
    controller.start();
    QObject::connect(&instance, &platform::SingleInstance::activationRequested, &shell, &ShellVm::activate);

    // Testing aid: `Semplice.exe --grab out.png` saves what the window shows and quits.
    if (parser.isSet(grabOption)) {
        const QString file = parser.value(grabOption);
        QTimer::singleShot(parser.value(devDelayOption).toInt(), &app, [window, file, &app] {
            window->grabWindow().save(file);
            app.quit();
        });
    }

    const int code = app.exec();
    controller.shutdown();
    SLOG_INFO(QStringLiteral("Semplice exiting with code %1").arg(code));
    return code;
}

}  // namespace semplice
