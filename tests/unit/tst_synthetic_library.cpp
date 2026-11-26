#include "IntegrityChecker.h"
#include "SyntheticLibrary.h"

#include "core/db/Migrator.h"
#include "core/services/SearchIndex.h"

#include <QtTest>

using namespace semplice;
using namespace semplice::testing;

class TstSyntheticLibrary : public QObject {
    Q_OBJECT
private slots:
    void smallPresetIsValid()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        const auto stats = generateSyntheticLibrary(*db, SyntheticSpec::small());
        QVERIFY2(stats.ok(), stats.ok() ? "" : qPrintable(stats.error().devMessage));
        QCOMPARE(stats.value().tracks, SyntheticSpec::small().tracks);
        QCOMPARE(stats.value().albums, SyntheticSpec::small().albums);
        QCOMPARE(checkLibraryIntegrity(*db).join(QLatin1Char('\n')), QString());
    }

    void sameSeedGivesTheSameLibrary()
    {
        auto digest = [](quint32 seed) {
            auto db = Database::open(QStringLiteral(":memory:")).value();
            if (!Migrator::migrate(*db).ok())
                qFatal("migration failed");
            SyntheticSpec spec = SyntheticSpec::small();
            spec.seed = seed;
            if (!generateSyntheticLibrary(*db, spec).ok())
                qFatal("generation failed");
            return db->scalar(QStringLiteral("SELECT group_concat(name, '|') FROM (SELECT name FROM artist ORDER BY id)"))
                .value()
                .toString();
        };
        QCOMPARE(digest(5), digest(5));
        QVERIFY(digest(5) != digest(6));
    }

    void tooSmallSpecIsRejected()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        SyntheticSpec spec;
        spec.albums = 500;
        spec.tracks = 100;  // fewer album tracks than albums
        QVERIFY(!generateSyntheticLibrary(*db, spec).ok());
    }

    void indexOfAGeneratedLibraryFindsItsOwnNames()
    {
        auto db = Database::open(QStringLiteral(":memory:")).value();
        QVERIFY(Migrator::migrate(*db).ok());
        QVERIFY(generateSyntheticLibrary(*db, SyntheticSpec::small()).ok());
        SearchIndex index;
        QVERIFY(index.rebuild(*db).ok());
        QCOMPARE(index.size(SearchKind::Track), 400);
        QCOMPARE(index.size(SearchKind::Playlist), 10);
        const SearchResults r = index.search(QStringLiteral("a"), 5);
        QVERIFY(r.tracks.total > 0);
    }
};

QTEST_GUILESS_MAIN(TstSyntheticLibrary)
#include "tst_synthetic_library.moc"
