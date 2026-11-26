// semplice_stopcheck: how long do stop() / setSource() block the calling thread while a file plays? (diagnostic)
#include <QAudioOutput>
#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <thread>

namespace {
bool writeWav(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const int rate = 8000, seconds = 30, bytes = rate * seconds;
    QByteArray h;
    auto le = [&](quint32 v, int n) { for (int i = 0; i < n; ++i) h.append(char((v >> (8 * i)) & 0xFF)); };
    h.append("RIFF"); le(36 + bytes, 4); h.append("WAVEfmt "); le(16, 4); le(1, 2); le(1, 2); le(rate, 4); le(rate, 4); le(1, 2); le(8, 2);
    h.append("data"); le(bytes, 4);
    QByteArray pcm(bytes, static_cast<char>(0x80));
    for (int k = 0; k < bytes; ++k)
        pcm[k] = char(128 + int(20 * (((k / 40) % 2) ? 1 : -1)));
    f.write(h);
    f.write(pcm);
    return true;
}
}  // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    QTemporaryDir dir;
    const QString wav = dir.filePath("a.wav");
    if (!writeWav(wav))
        return 1;
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("stop");
    QMediaPlayer player;
    QAudioOutput output;
    player.setAudioOutput(&output);
    int round = 0;
    QTimer cycle;
    cycle.setInterval(argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 2500);

    QObject::connect(&cycle, &QTimer::timeout, [&] {
        if (++round > 6) {
            app.quit();
            return;
        }
        QElapsedTimer t;
        t.start();
        if (mode == QLatin1String("stop")) {
            player.stop();
            const qint64 a = t.elapsed();
            player.setSource(QUrl());
            out << "round " << round << ": stop() " << a << " ms, then setSource(empty) " << t.elapsed() - a << " ms\n";
        } else if (mode == QLatin1String("direct")) {
            player.setSource(QUrl());
            out << "round " << round << ": setSource(empty) without stop " << t.elapsed() << " ms\n";
        } else if (mode == QLatin1String("blocking")) {
            // like the app: another thread asks the GUI thread to let go of the file and waits for it
            std::thread([&] {
                QMetaObject::invokeMethod(&player, [&] {
                    QElapsedTimer inner;
                    inner.start();
                    player.stop();
                    player.setSource(QUrl());
                    out << "round " << round << ": stop + unload inside a blocking call " << inner.elapsed() << " ms" << Qt::endl;
                    player.setSource(QUrl::fromLocalFile(wav));
                    player.play();
                }, Qt::BlockingQueuedConnection);
            }).detach();
            return;  // the event loop goes on (the other thread waits for it)
        } else if (mode == QLatin1String("pause")) {
            player.pause();
            const qint64 a = t.elapsed();
            player.setSource(QUrl());
            out << "round " << round << ": pause() " << a << " ms, then setSource(empty) " << t.elapsed() - a << " ms\n";
        }
        out.flush();
        player.setSource(QUrl::fromLocalFile(wav));
        player.play();
    });
    player.setSource(QUrl::fromLocalFile(wav));
    player.play();
    cycle.start();
    return app.exec();
}
