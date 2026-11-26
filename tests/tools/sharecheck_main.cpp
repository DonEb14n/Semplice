// semplice_sharecheck: can a file be renamed (and its folder) while Qt Multimedia plays it from a handle that was opened
// with FILE_SHARE_DELETE? Does the music go on?
#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "platform/SharedReadFile.h"

namespace {
bool writeWav(const QString& path)
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
        pcm[k] = char(128 + int(20 * ((k / 50) % 2 ? 1 : -1)));
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
    if (!writeWav(a))
        return 1;
    QMediaPlayer player;
    QAudioOutput output;
    player.setAudioOutput(&output);
    semplice::platform::SharedReadFile file(a);
    out << "open " << file.open() << Qt::endl;
    player.setSourceDevice(&file, QUrl::fromLocalFile(a));
    QObject::connect(&player, &QMediaPlayer::errorOccurred, [&](auto, const QString& t) { out << "ERROR " << t << Qt::endl; });
    player.play();
    QTimer::singleShot(1500, [&] {
        out << "position before: " << player.position() << "\n";
        out << "rename file while playing: " << (QFile::rename(a, dir.filePath("album/a2.wav")) ? "ok" : "REFUSED") << "\n";
        out << "rename folder while playing: " << (QDir().rename(dir.filePath("album"), dir.filePath("album2")) ? "ok" : "REFUSED") << "\n";
        out << "delete while playing: " << (QFile::remove(dir.filePath("album2/a2.wav")) ? "ok" : "REFUSED") << "\n";
        QTimer::singleShot(2500, [&] {
            out << "position after: " << player.position() << " state " << player.playbackState() << " error " << player.errorString() << "\n";
            out.flush();
            app.quit();
        });
    });
    return app.exec();
}
