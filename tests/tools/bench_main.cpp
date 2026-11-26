// semplice_bench --preset 50k|200k [--db file]
// Measures what exists at M1: generation, database size, startup (open + index build), search latency.
#include "SyntheticLibrary.h"

#include "core/db/Migrator.h"
#include "core/services/SearchIndex.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <random>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

using namespace semplice;
using namespace semplice::testing;

namespace {

double workingSetMb()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
#endif
    return 0.0;
}

struct Stats {
    double p50 = 0, p95 = 0, max = 0;
};

Stats summarise(QList<double> ms)
{
    std::sort(ms.begin(), ms.end());
    Stats s;
    if (ms.isEmpty())
        return s;
    s.p50 = ms[ms.size() / 2];
    s.p95 = ms[std::min<qsizetype>(ms.size() - 1, static_cast<qsizetype>(ms.size() * 0.95))];
    s.max = ms.last();
    return s;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("preset"), QStringLiteral("50k or 200k"), QStringLiteral("name"), QStringLiteral("50k")});
    parser.addOption({QStringLiteral("db"), QStringLiteral("database file (default: semplice_bench.db)"),
                      QStringLiteral("file"), QStringLiteral("semplice_bench.db")});
    parser.process(app);

    const QString preset = parser.value(QStringLiteral("preset"));
    const SyntheticSpec spec = preset == QLatin1String("200k") ? SyntheticSpec::hardLimit200k() : SyntheticSpec::target50k();
    const QString path = parser.value(QStringLiteral("db"));
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));

    out << "== preset " << preset << ": " << spec.tracks << " tracks, " << spec.albums << " albums, " << spec.artists
        << " artists, " << spec.playlists << " playlists ==\n";

    QElapsedTimer timer;
    {
        timer.start();
        auto db = Database::open(path);
        if (!db.ok() || !Migrator::migrate(*db.value()).ok())
            return 1;
        auto stats = generateSyntheticLibrary(*db.value(), spec);
        if (!stats.ok()) {
            out << "generation failed: " << stats.error().devMessage << "\n";
            return 1;
        }
        out << "generate (DB only):        " << timer.elapsed() << " ms\n";
        (void)db.value()->exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    }
    out << "database size:             " << QFileInfo(path).size() / (1024.0 * 1024.0) << " MB\n";

    // "Startup" = open + migrate check + build the in-memory index.
    const double memBefore = workingSetMb();
    timer.restart();
    auto db = Database::open(path);
    if (!db.ok() || !Migrator::migrate(*db.value()).ok())
        return 1;
    const qint64 openMs = timer.elapsed();
    SearchIndex index;
    timer.restart();
    if (!index.rebuild(*db.value()).ok())
        return 1;
    const qint64 indexMs = timer.elapsed();
    out << "open + migrate check:      " << openMs << " ms\n";
    out << "search index build:        " << indexMs << " ms (" << index.size(SearchKind::Track) << " tracks indexed)\n";
    out << "memory (working set) +:    " << (workingSetMb() - memBefore) << " MB\n";

    // Queries: 1..6 character fragments taken from real names, so every query has hits.
    QStringList samples;
    {
        auto r = db.value()->run(QStringLiteral("SELECT title FROM track ORDER BY id LIMIT 4000"));
        QSqlQuery q = std::move(r).value();
        while (q.next())
            samples << q.value(0).toString();
        auto r2 = db.value()->run(QStringLiteral("SELECT name FROM artist ORDER BY id LIMIT 1000"));
        QSqlQuery q2 = std::move(r2).value();
        while (q2.next())
            samples << q2.value(0).toString();
    }
    std::mt19937 gen(7);
    QList<double> latencies;
    QList<double> shortQueries;  // 1-2 characters match a huge part of the index: the worst case
    for (int i = 0; i < 400; ++i) {
        const QString& s = samples[static_cast<qsizetype>(gen() % static_cast<quint32>(samples.size()))];
        const int len = 1 + static_cast<int>(gen() % 6);
        const int start = static_cast<int>(gen() % static_cast<quint32>(std::max<qsizetype>(1, s.size() - len + 1)));
        const QString query = s.mid(start, len);
        QElapsedTimer t;
        t.start();
        const SearchResults r = index.search(query, 5);
        const double ms = static_cast<double>(t.nsecsElapsed()) / 1e6;
        Q_UNUSED(r);
        latencies << ms;
        if (query.trimmed().size() <= 2)
            shortQueries << ms;
    }
    const Stats all = summarise(latencies);
    const Stats shortStats = summarise(shortQueries);
    out << "search, 400 queries:       p50 " << all.p50 << " ms, p95 " << all.p95 << " ms, max " << all.max << " ms\n";
    out << "search, 1-2 char queries:  p50 " << shortStats.p50 << " ms, p95 " << shortStats.p95 << " ms, max "
        << shortStats.max << " ms (" << shortQueries.size() << " queries)\n";
    out << "budget: p95 < 100 ms (target size), < 250 ms (200k)\n";
    return 0;
}
