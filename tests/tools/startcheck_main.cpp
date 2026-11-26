// semplice_startcheck: how long does the real audio backend need from play() to the first sound (position > 0)?
//
//   semplice_startcheck <file1> [file2]
//
// Starts file1, waits a few seconds, then switches to file2 (or file1 again) while it plays.
#include "platform/QtAudioBackend.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTextStream>
#include <QTimer>

using namespace semplice;

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 2)
        return 2;
    const QString first = QString::fromLocal8Bit(argv[1]);
    const QString second = argc > 2 ? QString::fromLocal8Bit(argv[2]) : first;
    platform::QtAudioBackend backend;
    QElapsedTimer clock;
    bool waiting = false;
    QObject::connect(&backend, &IAudioBackend::positionChanged, &app, [&](qint64 ms) {
        if (waiting && ms > 0 && ms < 2500) {
            out << "first sound after " << clock.elapsed() << " ms (position " << ms << ")\n" << Qt::flush;
            waiting = false;
        }
    });
    QObject::connect(&backend, &IAudioBackend::errorOccurred, &app, [&](const QString& p, const QString& t) { out << "ERROR " << p << " " << t << Qt::endl; });
    auto start = [&](const QString& path, const char* what) {
        out << what << "\n" << Qt::flush;
        clock.start();
        waiting = true;
        backend.load(path);
        backend.play();
        out << "  load+play returned after " << clock.elapsed() << " ms\n" << Qt::flush;
    };
    start(first, "start (idle)");
    start(first, "immediate second load");
    QTimer::singleShot(4000, &app, [&] { start(second, "switch (while playing)"); });
    QTimer::singleShot(8000, &app, [&] { app.quit(); });
    return app.exec();
}
