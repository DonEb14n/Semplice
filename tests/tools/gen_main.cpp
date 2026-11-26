// semplice_gen --preset 50k|200k|small --out library.db [--seed N]
// Creates a migrated, populated library database for manual testing and benchmarks.
#include "IntegrityChecker.h"
#include "SyntheticLibrary.h"
#include "TestAudioFiles.h"

#include "core/db/Migrator.h"
#include "core/storage/LibraryFolder.h"
#include "core/tags/TagService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <cmath>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>

using namespace semplice;
using namespace semplice::testing;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("preset"), QStringLiteral("small, 50k or 200k"), QStringLiteral("name"),
                      QStringLiteral("small")});
    parser.addOption({QStringLiteral("out"), QStringLiteral("database file to create"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("seed"), QStringLiteral("random seed"), QStringLiteral("n"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("library"), QStringLiteral("create a library FOLDER (opens in the app; track files do not exist)"), QStringLiteral("folder")});
    parser.addOption({QStringLiteral("check"), QStringLiteral("run the integrity checker afterwards")});
    parser.addOption({QStringLiteral("wav"), QStringLiteral("with --samples: real playable .wav files instead of tiny mp3 stubs")});
    parser.addOption({QStringLiteral("count"), QStringLiteral("number of sample files"), QStringLiteral("n"), QStringLiteral("4")});
    parser.addOption({QStringLiteral("samples"), QStringLiteral("only write a few tiny sample audio files into FOLDER (to try drag and drop)"), QStringLiteral("folder")});
    parser.process(app);

    if (parser.isSet(QStringLiteral("samples"))) {
        const QString folder = parser.value(QStringLiteral("samples"));
        QDir().mkpath(folder);
        const int count = parser.value(QStringLiteral("count")).toInt();
        const bool wav = parser.isSet(QStringLiteral("wav"));
        for (int i = 1; i <= count; ++i)
        {
            if (!wav) {
                writeTinyAudio(folder, QStringLiteral("Sample Song %1").arg(i), AudioFormat::Mp3, i);
                continue;
            }
            // a real, playable file: 40 s of a quiet tone (the first seconds differ per file so the content hashes differ)
            QFile f(QDir(folder).filePath(QStringLiteral("Sample Song %1.wav").arg(i)));
            if (!f.open(QIODevice::WriteOnly))
                return 1;
            const int rate = 8000, bytes = rate * 40;
            QByteArray h;
            auto le = [&](quint32 v, int n) { for (int k = 0; k < n; ++k) h.append(char((v >> (8 * k)) & 0xFF)); };
            h.append("RIFF"); le(36 + bytes, 4); h.append("WAVEfmt "); le(16, 4); le(1, 2); le(1, 2); le(rate, 4); le(rate, 4); le(1, 2); le(8, 2);
            h.append("data"); le(bytes, 4);
            QByteArray pcm(bytes, static_cast<char>(0x80));
            for (int k = 0; k < bytes; ++k)
                pcm[k] = char(128 + int(20 * std::sin(k * (0.05 + 0.01 * i))));
            f.write(h);
            f.write(pcm);
            f.close();
            // tags, so that the importer proposes one album with one artist
            PortableAtomicReplace replace;
            TagService tags(replace);
            TagWrite tw;
            tw.title = QStringLiteral("Stall Song %1").arg(i);
            tw.artists = {QStringLiteral("Stall Artist")};
            tw.album = QStringLiteral("Stall Album");
            tw.albumArtists = QStringList{QStringLiteral("Stall Artist")};
            tw.trackNumber = i;
            tw.date = ReleaseDate{2020, 1, 1};
            (void)tags.write(f.fileName(), tw);
        }
        out << "wrote " << count << " sample files to " << folder << Qt::endl;
        return 0;
    }
    SyntheticSpec spec = SyntheticSpec::small();
    const QString preset = parser.value(QStringLiteral("preset"));
    if (preset == QLatin1String("50k"))
        spec = SyntheticSpec::target50k();
    else if (preset == QLatin1String("200k"))
        spec = SyntheticSpec::hardLimit200k();
    else if (preset != QLatin1String("small")) {
        out << "unknown preset " << preset << "\n";
        return 2;
    }
    spec.seed = parser.value(QStringLiteral("seed")).toUInt();
    QString path = parser.value(QStringLiteral("out"));
    if (parser.isSet(QStringLiteral("library"))) {
        NullFileAttributes attributes;
        const QString folder = parser.value(QStringLiteral("library"));
        auto created = LibraryFolder::create(folder, attributes);
        if (!created.ok()) {
            out << "cannot create the library folder: " << created.error().symbol() << "\n";
            return 1;
        }
        path = LibraryLayout::forRoot(folder).dbPath;
    }
    if (path.isEmpty()) {
        out << "--out or --library is required\n";
        return 2;
    }
    QFile::remove(path);

    QElapsedTimer timer;
    timer.start();
    auto db = Database::open(path);
    if (!db.ok() || !Migrator::migrate(*db.value()).ok()) {
        out << "cannot create database\n";
        return 1;
    }
    auto stats = generateSyntheticLibrary(*db.value(), spec);
    if (!stats.ok()) {
        out << "generation failed: " << stats.error().devMessage << "\n";
        return 1;
    }
    out << "generated " << stats.value().tracks << " tracks, " << stats.value().albums << " albums, "
        << stats.value().artists << " artists (" << stats.value().hiddenArtists << " hidden), "
        << stats.value().playlists << " playlists in " << timer.elapsed() << " ms\n";

    if (parser.isSet(QStringLiteral("check"))) {
        const QStringList problems = checkLibraryIntegrity(*db.value());
        out << (problems.isEmpty() ? "integrity: OK\n" : "integrity: PROBLEMS\n");
        for (const QString& p : problems)
            out << "  " << p << "\n";
        return problems.isEmpty() ? 0 : 1;
    }
    return 0;
}
