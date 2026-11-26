#include "StorageTestSupport.h"

#include "core/db/Migrator.h"
#include "core/storage/DbCommitLog.h"

using namespace semplice;
using namespace semplice::testing;

namespace {

// Records every checkpoint and never crashes: used to discover where a scenario can be interrupted.
class RecordingInjector final : public FaultInjector {
public:
    bool crashHere(const QString& name) override
    {
        names << name;
        return false;
    }
    QStringList names;
};

// Simulates power loss at the n-th checkpoint.
class CrashAt final : public FaultInjector {
public:
    explicit CrashAt(int n) : m_target(n) {}
    bool crashHere(const QString& name) override
    {
        if (m_seen++ != m_target)
            return false;
        where = name;
        return true;
    }
    QString where;

private:
    int m_target;
    int m_seen = 0;
};

#define TRY(expr)                              \
    do {                                       \
        auto try_result = (expr);              \
        if (!try_result.ok())                  \
            return try_result.error();         \
    } while (false)

struct Scenario {
    QString name;
    bool autoCommit = false;  // true: FileStorageService::apply() (no database involved)
    std::function<void(StorageEnv&)> arrange;
    std::function<QList<FileOp>(StorageEnv&)> ops;
};

QList<Scenario> scenarios()
{
    auto copyOp = [](const QString& from, const QString& to) { return FileOp{FileOp::Kind::Copy, from, to}; };
    QList<Scenario> list;

    list.append({QStringLiteral("import: new folders and three copies"), false,
                 [](StorageEnv& e) {
                     e.mkdirLib(QStringLiteral("Artists"));
                     e.mkdirLib(QStringLiteral("Playlists"));
                     e.write(e.source(QStringLiteral("a.mp3")), QByteArray("AAAAAAAAAA"));
                     e.write(e.source(QStringLiteral("b.mp3")), QByteArray("BBBBBBBBBB"));
                     e.write(e.source(QStringLiteral("c.mp3")), QByteArray("CCCCCCCCCC"));
                 },
                 [copyOp](StorageEnv& e) -> QList<FileOp> {
                     return {{FileOp::Kind::EnsureDir, QStringLiteral("Artists/New Band/First Album"), {}},
                             copyOp(e.source(QStringLiteral("a.mp3")), QStringLiteral("Artists/New Band/First Album/01 - a.mp3")),
                             copyOp(e.source(QStringLiteral("b.mp3")), QStringLiteral("Artists/New Band/First Album/02 - b.mp3")),
                             copyOp(e.source(QStringLiteral("c.mp3")), QStringLiteral("Artists/New Band/First Album/03 - c.mp3"))};
                 }});

    list.append({QStringLiteral("re-home: folder move + file move into a new folder"), false,
                 [](StorageEnv& e) {
                     e.writeLib(QStringLiteral("Artists/A/Together/01 - x.mp3"), QByteArray("x"));
                     e.writeLib(QStringLiteral("Artists/A/Together/02 - y.mp3"), QByteArray("y"));
                     e.mkdirLib(QStringLiteral("Artists/B"));
                     e.writeLib(QStringLiteral("Playlists/P/s.mp3"), QByteArray("s"));
                 },
                 [](StorageEnv&) -> QList<FileOp> {
                     return {{FileOp::Kind::MoveDir, QStringLiteral("Artists/A/Together"), QStringLiteral("Artists/B/Together")},
                             {FileOp::Kind::MoveFile, QStringLiteral("Playlists/P/s.mp3"),
                              QStringLiteral("Artists/A/Unsorted tracks/01 - s.mp3")}};
                 }});

    list.append({QStringLiteral("delete album: home move, then the leftovers go to the Recycle Bin"), false,
                 [](StorageEnv& e) {
                     e.writeLib(QStringLiteral("Artists/A/Old/01 - a.mp3"), QByteArray("a"));
                     e.writeLib(QStringLiteral("Artists/A/Old/02 - b.mp3"), QByteArray("b"));
                     e.writeLib(QStringLiteral("Artists/A/Old/cover.jpg"), QByteArray("cover"));
                     e.mkdirLib(QStringLiteral("Playlists/P"));
                 },
                 [](StorageEnv&) -> QList<FileOp> {
                     return {{FileOp::Kind::MoveFile, QStringLiteral("Artists/A/Old/02 - b.mp3"), QStringLiteral("Playlists/P/b.mp3")},
                             {FileOp::Kind::Trash, QStringLiteral("Artists/A/Old/01 - a.mp3"), {}},
                             {FileOp::Kind::Trash, QStringLiteral("Artists/A/Old"), {}}};
                 }});

    list.append({QStringLiteral("case-only rename of an artist folder"), false,
                 [](StorageEnv& e) {
                     e.writeLib(QStringLiteral("Artists/abba/Gold/01.mp3"), QByteArray("g"));
                     e.writeLib(QStringLiteral("Artists/abba/avatar.jpg"), QByteArray("v"));
                 },
                 [](StorageEnv&) -> QList<FileOp> {
                     return {{FileOp::Kind::MoveDir, QStringLiteral("Artists/abba"), QStringLiteral("Artists/ABBA")}};
                 }});

    list.append({QStringLiteral("one-shot apply(): create, move, trash in order"), true,
                 [](StorageEnv& e) {
                     e.writeLib(QStringLiteral("Artists/A/Old/01 - a.mp3"), QByteArray("a"));
                     e.writeLib(QStringLiteral("Artists/A/Old/02 - b.mp3"), QByteArray("b"));
                 },
                 [](StorageEnv&) -> QList<FileOp> {
                     return {{FileOp::Kind::EnsureDir, QStringLiteral("Playlists/New"), {}},
                             {FileOp::Kind::MoveFile, QStringLiteral("Artists/A/Old/02 - b.mp3"), QStringLiteral("Playlists/New/b.mp3")},
                             {FileOp::Kind::Trash, QStringLiteral("Artists/A/Old"), {}}};
                 }});
    return list;
}

QString dbPathOf(const StorageEnv& e)
{
    return e.root + QStringLiteral("/.library/library.db");
}

// begin -> execute -> DB transaction (marker + commit log) -> finish. Stops at the injected crash.
Result<void> runFlow(StorageEnv& env, Database* db, const Scenario& s)
{
    const QList<FileOp> ops = s.ops(env);
    if (s.autoCommit)
        return env.storage->apply(ops);

    auto op = env.storage->begin(ops, s.name);
    if (!op.ok())
        return op.error();
    auto executed = op.value()->execute();
    if (!executed.ok())
        return executed.error();
    {
        Transaction tx(*db);
        TRY(tx.begin());
        TRY(db->exec(QStringLiteral("INSERT INTO kv(key, value) VALUES('marker', ?)"), {op.value()->id()}));
        TRY(DbCommitLog::mark(*db, op.value()->id(), 1));
        TRY(env.storage->checkpoint(QStringLiteral("db:before-commit")));  // crash here = transaction lost
        TRY(tx.commit());
    }
    TRY(env.storage->checkpoint(QStringLiteral("db:after-commit")));
    return op.value()->finish();
}

}  // namespace

class TstJournalRecovery : public QObject {
    Q_OBJECT

    static std::unique_ptr<Database> openDb(const StorageEnv& env)
    {
        auto db = Database::open(dbPathOf(env));
        if (!db.ok())
            qFatal("cannot open test database");
        auto migrated = Migrator::migrate(*db.value());
        if (!migrated.ok())
            qFatal("migration failed");
        Q_UNUSED(migrated);
        return std::move(db).value();
    }

    static bool markerPresent(Database& db)
    {
        return db.scalar(QStringLiteral("SELECT COUNT(*) FROM kv WHERE key = 'marker'")).value().toInt() > 0;
    }

private slots:
    // The heart of M2: interrupt every scenario at every checkpoint, "restart", recover, compare.
    void everyCrashPointLeavesDiskAndDatabaseConsistent()
    {
        for (const Scenario& scenario : scenarios()) {
            // 1. Reference run: where can it be interrupted, what is "before", what is "after"?
            TreeSnapshot expectedBefore, expectedAfter, trashAfter, sourceBefore;
            QStringList checkpoints;
            {
                StorageEnv env;
                env.storage->setChunkSize(4);
                scenario.arrange(env);
                expectedBefore = env.libraryTree();
                sourceBefore = env.sourceTree();
                auto db = openDb(env);
                RecordingInjector recorder;
                env.storage->setFaultInjector(&recorder);
                QVERIFY2(runFlow(env, db.get(), scenario).ok(), qPrintable(scenario.name));
                checkpoints = recorder.names;
                expectedAfter = env.libraryTree();
                trashAfter = env.trashTree();
                QVERIFY(expectedBefore != expectedAfter);
                QCOMPARE(env.journalEntries(), 0);
            }
            QVERIFY2(checkpoints.size() >= 5, qPrintable(scenario.name));

            // 2. One run per checkpoint.
            for (int i = 0; i < checkpoints.size(); ++i) {
                const QString label = QStringLiteral("%1 | crash at #%2 '%3'").arg(scenario.name).arg(i).arg(checkpoints[i]);
                StorageEnv env;
                env.storage->setChunkSize(4);
                scenario.arrange(env);
                CrashAt crash(i);
                bool committed = false;
                {
                    auto db = openDb(env);
                    env.storage->setFaultInjector(&crash);
                    const auto flow = runFlow(env, db.get(), scenario);
                    QVERIFY2(!flow.ok() && flow.error().code == ErrorCode::CrashInjected, qPrintable(label));
                    QCOMPARE(crash.where, checkpoints[i]);
                    // the process "dies": the old storage object and connection are simply dropped
                    env.storage = std::make_unique<FileStorageService>(env.root, env.journalDir(), env.trash, env.clock);
                }

                // 3. The next start: recover (twice: it must be idempotent), then judge the result.
                auto db = openDb(env);
                committed = scenario.autoCommit || markerPresent(*db);
                for (int pass = 1; pass <= 2; ++pass) {
                    const auto report = env.storage->recover(
                        [&](const QString& id) { return DbCommitLog::isCommitted(*db, id); });
                    QVERIFY2(report.ok(), qPrintable(label));
                    QVERIFY2(report.value().problems.isEmpty(), qPrintable(label + report.value().problems.join(QLatin1Char(';'))));
                    if (pass == 2)
                        QCOMPARE(report.value().rolledForward + report.value().rolledBack, 0);
                }
                QVERIFY2(env.journalEntries() == 0, qPrintable(label));

                const TreeSnapshot libraryNow = env.libraryTree();
                const TreeSnapshot& expected = committed ? expectedAfter : expectedBefore;
                QVERIFY2(libraryNow == expected,
                         qPrintable(QStringLiteral("%1\n committed=%2\n--- actual\n%3\n--- expected\n%4")
                                        .arg(label)
                                        .arg(committed)
                                        .arg(libraryNow.describe(), expected.describe())));
                QVERIFY2(env.trashTree() == (committed ? trashAfter : TreeSnapshot{}), qPrintable(label + " (trash)"));
                QVERIFY2(env.sourceTree() == sourceBefore, qPrintable(label + " (source files must never change)"));
            }
        }
    }

    // A crash while recovery itself is running must not make things worse: simply recover again.
    void recoveryIsRestartable()
    {
        const Scenario scenario = scenarios().at(2);  // delete album (has a trash phase)
        StorageEnv env;
        scenario.arrange(env);
        const TreeSnapshot before = env.libraryTree();
        {
            auto db = openDb(env);
            CrashAt crash(8);  // somewhere in phase B / after the commit
            env.storage->setFaultInjector(&crash);
            QVERIFY(!runFlow(env, db.get(), scenario).ok());
        }
        auto db = openDb(env);
        const auto oracle = [&](const QString& id) { return DbCommitLog::isCommitted(*db, id); };
        env.storage = std::make_unique<FileStorageService>(env.root, env.journalDir(), env.trash, env.clock);
        QVERIFY(env.storage->recover(oracle).ok());
        QVERIFY(env.storage->recover(oracle).ok());
        QCOMPARE(env.journalEntries(), 0);
        Q_UNUSED(before);
    }

    // The conservation property, stated on its own: whatever the crash point, every original file's
    // content still exists somewhere in the library or the Recycle Bin after recovery.
    void noFileContentEverDisappears()
    {
        const Scenario scenario = scenarios().at(2);
        TreeSnapshot start;
        int checkpointCount = 0;
        {
            StorageEnv env;
            scenario.arrange(env);
            start = env.libraryTree();
            auto db = openDb(env);
            RecordingInjector recorder;
            env.storage->setFaultInjector(&recorder);
            QVERIFY(runFlow(env, db.get(), scenario).ok());
            checkpointCount = recorder.names.size();
        }
        for (int i = 0; i < checkpointCount; ++i) {
            StorageEnv env;
            scenario.arrange(env);
            {
                auto db = openDb(env);
                CrashAt crash(i);
                env.storage->setFaultInjector(&crash);
                (void)runFlow(env, db.get(), scenario);
            }
            env.storage = std::make_unique<FileStorageService>(env.root, env.journalDir(), env.trash, env.clock);
            auto db = openDb(env);
            QVERIFY(env.storage->recover([&](const QString& id) { return DbCommitLog::isCommitted(*db, id); }).ok());

            QList<QByteArray> contents = env.libraryTree().files.values();
            contents += env.trashTree().files.values();
            for (const QByteArray& original : start.files.values())
                QVERIFY2(contents.contains(original), qPrintable(QStringLiteral("lost %1 at crash #%2").arg(QString::fromUtf8(original)).arg(i)));
        }
    }
};

QTEST_GUILESS_MAIN(TstJournalRecovery)
#include "tst_journal_recovery.moc"
