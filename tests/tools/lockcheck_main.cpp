// semplice_lockcheck: how does Qt Multimedia behave while we rename / delete / reopen the file it plays?
// Writes two 20 s wav files, plays the first, and reports (a) whether the file can be renamed while it plays,
// (b) how long the GUI thread is blocked when the source is changed (a heartbeat timer measures the gaps).
#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

namespace {
bool writeWav(const QString& path, double pitch)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const int rate = 8000, seconds = 20, bytes = rate * seconds;
    QByteArray h;
    auto le = [&](quint32 v, int n) { for (int i = 0; i < n; ++i) h.append(char((v >> (8 * i)) & 0xFF)); };
    h.append("RIFF"); le(36 + bytes, 4); h.append("WAVEfmt "); le(16, 4); le(1, 2); le(1, 2); le(rate, 4); le(rate, 4); le(1, 2); le(8, 2);
    h.append("data"); le(bytes, 4);
    QByteArray pcm(bytes, static_cast<char>(0x80));
    for (int k = 0; k < bytes; ++k)
        pcm[k] = char(128 + int(20 * ((int(k * pitch) / 50) % 2 ? 1 : -1)));
    f.write(h);
    f.write(pcm);
    return true;
}
}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTemporaryDir dir;
    QDir().mkpath(dir.filePath("album"));
    const QString a = dir.filePath("album/a.wav");
    const QString b = dir.filePath("album/b.wav");
    if (!writeWav(a, 1.0) || !writeWav(b, 1.5))
        return 1;

    QMediaPlayer player;
    QAudioOutput output;
    player.setAudioOutput(&output);

    QElapsedTimer beat;
    beat.start();
    qint64 worst = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        const qint64 gap = beat.restart();
        worst = std::max(worst, gap);
    });
    heartbeat.start();
    auto report = [&](const char* what) {
        out << what << ": worst GUI gap " << worst << " ms\n";
        out.flush();
        worst = 0;
    };

    player.setSource(QUrl::fromLocalFile(a));
    player.play();
    QTimer::singleShot(1500, [&] {
        report("first play");
        out << "rename while playing: " << (QFile::rename(a, dir.filePath("album/a2.wav")) ? "ok" : "REFUSED") << "\n";
        player.stop();
        player.setSource(QUrl());
        out << "rename after unload: " << (QFile::rename(a, dir.filePath("album/a2.wav")) ? "ok" : "REFUSED") << "\n";
        report("stop + unload");
        player.setSource(QUrl::fromLocalFile(dir.filePath("album/a2.wav")));
        player.play();
        QTimer::singleShot(1500, [&] {
            report("reopen the same track");
            player.setPosition(5000);
            QTimer::singleShot(300, [&] { report("seek to 5 s"); });
            player.setSource(QUrl::fromLocalFile(b));
            player.play();
            QTimer::singleShot(1500, [&] {
                report("next track");
                app.quit();
            });
        });
    });
    return app.exec();
}
