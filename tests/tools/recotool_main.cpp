// semplice_recotool: prints the best recommendation candidates with every score component, to tune the weights.
//
//   semplice_recotool <library folder> --album <id> [--top 20]
//   semplice_recotool <library folder> --playlist <id> [--top 20]
//
// Read-only: the library is opened for reading and nothing is changed.
#include "core/db/Database.h"
#include "core/recommend/LibraryRecommendationSource.h"
#include "core/storage/LibraryFolder.h"
#include "core/tags/GenreVocabulary.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTextStream>

using namespace semplice;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("library"), QStringLiteral("library folder"));
    parser.addOption({QStringLiteral("album"), QStringLiteral("seed: the tracks of this album id"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("playlist"), QStringLiteral("seed: the tracks of this playlist id"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("top"), QStringLiteral("how many candidates to print"), QStringLiteral("n"), QStringLiteral("20")});
    parser.process(app);
    if (parser.positionalArguments().isEmpty() || !(parser.isSet(QStringLiteral("album")) || parser.isSet(QStringLiteral("playlist")))) {
        out << parser.helpText();
        return 2;
    }
    const QString root = parser.positionalArguments().first();
    auto db = Database::open(LibraryLayout::forRoot(root).dbPath, true);
    if (!db.ok()) {
        out << "cannot open the library: " << db.error().symbol() << "\n";
        return 1;
    }
    auto genres = GenreVocabulary::load();
    if (!genres.ok())
        return 1;
    auto pool = loadRecommendationPool(*db.value(), genres.value());
    if (!pool.ok()) {
        out << "cannot read the library: " << pool.error().symbol() << "\n";
        return 1;
    }

    const bool album = parser.isSet(QStringLiteral("album"));
    auto rows = db.value()->run(album ? QStringLiteral("SELECT track_id FROM album_track WHERE album_id = ?")
                                      : QStringLiteral("SELECT track_id FROM playlist_track WHERE playlist_id = ?"),
                                {parser.value(album ? QStringLiteral("album") : QStringLiteral("playlist")).toLongLong()});
    if (!rows.ok())
        return 1;
    PickRequest request;
    request.nowMs = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery q = std::move(rows).value();
    while (q.next())
        request.seeds.append(TrackId(q.value(0).toLongLong()));
    out << "seed tracks: " << request.seeds.size() << ", pool: " << pool.value().tracks.size() << "\n";

    MersenneRandom random(1);
    Selector selector({}, random);
    const QStringList keys = genres.value().keys();
    selector.setPool(pool.value().tracks, pool.value().relations);
    {
        QElapsedTimer timer;
        timer.start();
        PickRequest one = request;
        one.count = 3;
        const int n = static_cast<int>(selector.pick(one).size());
        out << "pick of " << n << " tracks took " << timer.elapsed() << " ms on this pool" << Qt::endl;
    }
    out << "id        total   tags   artist related year   opposite recent  album\n";
    for (const Selector::Candidate& c : selector.top(request, parser.value(QStringLiteral("top")).toInt())) {
        auto cell = [&](double v) { return QString::number(v, 'f', 2).rightJustified(7); };
        out << QString::number(c.id.value).leftJustified(9) << cell(c.parts.total()) << cell(c.parts.tags) << cell(c.parts.artist)
            << cell(c.parts.related) << cell(c.parts.year) << cell(c.parts.opposite) << cell(c.parts.recent) << "  " << c.album << "\n";
    }
    return 0;
}
