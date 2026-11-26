#include "StorageTestSupport.h"

#include "core/storage/OwnFiles.h"
#include "core/storage/ITrash.h"

#include <QCryptographicHash>

using namespace semplice;
using namespace semplice::testing;

class TstFileStorage : public QObject {
    Q_OBJECT

    static FileOp copy(const QString& from, const QString& to) { return {FileOp::Kind::Copy, from, to}; }
    static FileOp moveFile(const QString& from, const QString& to) { return {FileOp::Kind::MoveFile, from, to}; }
    static FileOp moveDir(const QString& from, const QString& to) { return {FileOp::Kind::MoveDir, from, to}; }
    static FileOp trash(const QString& path) { return {FileOp::Kind::Trash, path, {}}; }
    static FileOp ensureDir(const QString& path) { return {FileOp::Kind::EnsureDir, path, {}}; }

    // Runs begin + execute + finish; the common happy path.
    static Result<QList<CopyResult>> runAll(StorageEnv& env, const QList<FileOp>& ops, const ExecuteOptions& opts = {})
    {
        auto op = env.storage->begin(ops);
        if (!op.ok())
            return op.error();
        auto done = op.value()->execute(opts);
        if (!done.ok())
            return done.error();
        if (auto f = op.value()->finish(); !f.ok())
            return f.error();
        return done;
    }

private slots:
    // ================================================================ copy
    void copyComputesHashAndSizeAndNeverTouchesTheSource()
    {
        StorageEnv env;
        const QByteArray content("0123456789 the original bytes");
        env.write(env.source(QStringLiteral("song.mp3")), content);
        const QFileInfo before(env.source(QStringLiteral("song.mp3")));
        const QDateTime mtime = before.lastModified();

        const auto r = runAll(env, {copy(env.source(QStringLiteral("song.mp3")), QStringLiteral("Artists/A/Alb/01 - Song.mp3"))});
        QVERIFY(r.ok());
        QCOMPARE(r.value().size(), 1);
        QCOMPARE(r.value().first().sha256, QCryptographicHash::hash(content, QCryptographicHash::Sha256));
        QCOMPARE(r.value().first().size, qint64(content.size()));
        QCOMPARE(env.read(env.lib(QStringLiteral("Artists/A/Alb/01 - Song.mp3"))), content);
        QCOMPARE(env.read(env.source(QStringLiteral("song.mp3"))), content);
        QCOMPARE(QFileInfo(env.source(QStringLiteral("song.mp3"))).lastModified(), mtime);
        QCOMPARE(env.journalEntries(), 0);
    }

    void copyOfAnEmptyFileWorks()
    {
        StorageEnv env;
        env.write(env.source(QStringLiteral("empty.mp3")), {});
        const auto r = runAll(env, {copy(env.source(QStringLiteral("empty.mp3")), QStringLiteral("Artists/A/e.mp3"))});
        QVERIFY(r.ok());
        QCOMPARE(r.value().first().sha256, QCryptographicHash::hash({}, QCryptographicHash::Sha256));
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/A/e.mp3"))));
    }

    void copyReportsMonotonicProgress()
    {
        StorageEnv env;
        env.storage->setChunkSize(4);
        env.write(env.source(QStringLiteral("a.mp3")), QByteArray("0123456789"));
        env.write(env.source(QStringLiteral("b.mp3")), QByteArray("abcdefghij"));
        QList<CopyProgress> seen;
        ExecuteOptions opts;
        opts.progress = [&](const CopyProgress& p) { seen << p; };
        QVERIFY(runAll(env,
                       {copy(env.source(QStringLiteral("a.mp3")), QStringLiteral("Artists/x/a.mp3")),
                        copy(env.source(QStringLiteral("b.mp3")), QStringLiteral("Artists/x/b.mp3"))},
                       opts)
                    .ok());
        QVERIFY(seen.size() >= 6);  // 3 chunks per file
        qint64 last = 0;
        for (const CopyProgress& p : std::as_const(seen)) {
            QVERIFY(p.bytesDone >= last);
            QCOMPARE(p.bytesTotal, qint64(20));
            QCOMPARE(p.fileCount, 2);
            last = p.bytesDone;
        }
        QCOMPARE(seen.last().bytesDone, qint64(20));
        QCOMPARE(seen.first().fileIndex, 1);
        QCOMPARE(seen.last().fileIndex, 2);
    }

    void cancelUndoesEverythingTheOperationDid()
    {
        StorageEnv env;
        env.storage->setChunkSize(4);
        env.write(env.source(QStringLiteral("a.mp3")), QByteArray("0123456789"));
        env.write(env.source(QStringLiteral("b.mp3")), QByteArray("abcdefghij"));
        env.mkdirLib(QStringLiteral("Artists"));
        const TreeSnapshot before = env.libraryTree();
        const TreeSnapshot sourceBefore = env.sourceTree();

        std::stop_source stop;
        ExecuteOptions opts;
        opts.cancel = stop.get_token();
        opts.progress = [&](const CopyProgress& p) {
            if (p.fileIndex == 2)
                stop.request_stop();  // user presses Cancel in the middle of the second file
        };
        const auto r = runAll(env,
                              {ensureDir(QStringLiteral("Artists/New")),
                               copy(env.source(QStringLiteral("a.mp3")), QStringLiteral("Artists/New/Alb/a.mp3")),
                               copy(env.source(QStringLiteral("b.mp3")), QStringLiteral("Artists/New/Alb/b.mp3"))},
                              opts);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::Cancelled);
        QVERIFY2(env.libraryTree() == before, qPrintable(env.libraryTree().describe()));
        QVERIFY(env.sourceTree() == sourceBefore);
        QCOMPARE(env.journalEntries(), 0);
    }

    void anExistingDestinationIsNeverOverwrittenOrRemoved()
    {
        StorageEnv env;
        env.write(env.source(QStringLiteral("a.mp3")), QByteArray("new"));
        env.writeLib(QStringLiteral("Artists/A/Alb/01.mp3"), QByteArray("precious"));
        const auto r = runAll(env, {copy(env.source(QStringLiteral("a.mp3")), QStringLiteral("Artists/A/Alb/01.mp3"))});
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::AlreadyExists);
        QCOMPARE(env.read(env.lib(QStringLiteral("Artists/A/Alb/01.mp3"))), QByteArray("precious"));
        QCOMPARE(env.journalEntries(), 0);
    }

    void aMissingSourceIsRefusedBeforeAnythingHappens()
    {
        StorageEnv env;
        const auto op = env.storage->begin({copy(env.source(QStringLiteral("nope.mp3")), QStringLiteral("Artists/A/x.mp3"))});
        QVERIFY(!op.ok());
        QCOMPARE(op.error().code, ErrorCode::NotFound);
        QCOMPARE(env.journalEntries(), 0);
    }

    // ================================================================ paths
    void unsafeLibraryPathsAreRejected_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("parent") << QStringLiteral("../outside.mp3");
        QTest::newRow("nested parent") << QStringLiteral("Artists/../../outside.mp3");
        QTest::newRow("absolute") << QStringLiteral("/etc/passwd");
        QTest::newRow("drive") << QStringLiteral("C:/Windows/x");
        QTest::newRow("drive relative") << QStringLiteral("C:x");
        QTest::newRow("library folder") << QStringLiteral(".library/library.db");
        QTest::newRow("library folder, other case") << QStringLiteral(".LIBRARY/journal/x");
        QTest::newRow("empty") << QString();
        QTest::newRow("dot") << QStringLiteral(".");
    }
    void unsafeLibraryPathsAreRejected()
    {
        QFETCH(QString, path);
        StorageEnv env;
        env.write(env.source(QStringLiteral("a.mp3")), QByteArray("x"));
        QCOMPARE(env.storage->begin({ensureDir(path)}).error().code, ErrorCode::InvalidArgument);
        QCOMPARE(env.storage->begin({copy(env.source(QStringLiteral("a.mp3")), path)}).error().code, ErrorCode::InvalidArgument);
        QCOMPARE(env.storage->begin({trash(path)}).error().code, ErrorCode::InvalidArgument);
        QCOMPARE(env.storage->begin({moveFile(QStringLiteral("a"), path)}).error().code, ErrorCode::InvalidArgument);
        QCOMPARE(env.journalEntries(), 0);
    }

    void veryLongPathsReportPathTooLong()
    {
        StorageEnv env;
        env.write(env.source(QStringLiteral("a.mp3")), QByteArray("x"));
        QString longPath = QStringLiteral("Artists");
        while (longPath.size() < 33000)
            longPath += QStringLiteral("/") + QString(100, QLatin1Char('d'));
        const auto op = env.storage->begin({copy(env.source(QStringLiteral("a.mp3")), longPath + QStringLiteral("/a.mp3"))});
        QVERIFY(!op.ok());
        QCOMPARE(op.error().code, ErrorCode::PathTooLong);
    }

    // ================================================================ moves
    void movesCreateMissingParentFolders()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Playlists/P/s.mp3"), QByteArray("s"));
        QVERIFY(runAll(env, {moveFile(QStringLiteral("Playlists/P/s.mp3"), QStringLiteral("Artists/A/Unsorted tracks/01 - s.mp3"))}).ok());
        QCOMPARE(env.read(env.lib(QStringLiteral("Artists/A/Unsorted tracks/01 - s.mp3"))), QByteArray("s"));
        QVERIFY(!QFileInfo::exists(env.lib(QStringLiteral("Playlists/P/s.mp3"))));
    }

    void movingAFolderMovesItsContents()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Artists/A/Together/01 - x.mp3"), QByteArray("x"));
        env.writeLib(QStringLiteral("Artists/A/Together/cover.jpg"), QByteArray("c"));
        env.mkdirLib(QStringLiteral("Artists/B"));
        QVERIFY(runAll(env, {moveDir(QStringLiteral("Artists/A/Together"), QStringLiteral("Artists/B/Together"))}).ok());
        QCOMPARE(env.read(env.lib(QStringLiteral("Artists/B/Together/01 - x.mp3"))), QByteArray("x"));
        QCOMPARE(env.read(env.lib(QStringLiteral("Artists/B/Together/cover.jpg"))), QByteArray("c"));
        QVERIFY(!QFileInfo::exists(env.lib(QStringLiteral("Artists/A/Together"))));
    }

    void caseOnlyRenamesWorkForFoldersAndFiles()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Artists/abba/Gold/track.mp3"), QByteArray("t"));
        QVERIFY(runAll(env, {moveDir(QStringLiteral("Artists/abba"), QStringLiteral("Artists/ABBA"))}).ok());
        QVERIFY(QDir(env.lib(QStringLiteral("Artists"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot).contains(QStringLiteral("ABBA")));
        QVERIFY(!QDir(env.lib(QStringLiteral("Artists"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot).contains(QStringLiteral("abba")));
        QCOMPARE(env.read(env.lib(QStringLiteral("Artists/ABBA/Gold/track.mp3"))), QByteArray("t"));

        QVERIFY(runAll(env, {moveFile(QStringLiteral("Artists/ABBA/Gold/track.mp3"), QStringLiteral("Artists/ABBA/Gold/Track.mp3"))}).ok());
        QVERIFY(QDir(env.lib(QStringLiteral("Artists/ABBA/Gold"))).entryList(QDir::Files).contains(QStringLiteral("Track.mp3")));
        QCOMPARE(env.journalEntries(), 0);
        // no temporary name left behind
        QCOMPARE(QDir(env.lib(QStringLiteral("Artists"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).size(), 1);
    }

    void aFailingStepUndoesTheStepsBeforeIt()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Artists/A/a.mp3"), QByteArray("a"));
        env.writeLib(QStringLiteral("Artists/A/b.mp3"), QByteArray("b"));
        env.writeLib(QStringLiteral("Artists/A/c.mp3"), QByteArray("c"));
        const TreeSnapshot before = env.libraryTree();
        const auto r = runAll(env, {moveFile(QStringLiteral("Artists/A/a.mp3"), QStringLiteral("Artists/Z/a.mp3")),
                                    moveFile(QStringLiteral("Artists/A/b.mp3"), QStringLiteral("Artists/A/c.mp3"))});  // c exists
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::AlreadyExists);
        QVERIFY2(env.libraryTree() == before, qPrintable(env.libraryTree().describe()));  // incl. the new Artists/Z folder gone
    }

    // ================================================================ trash
    void deletionsGoToTheRecycleBinAndOnlyAfterFinish()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Artists/A/Old/01.mp3"), QByteArray("one"));
        env.writeLib(QStringLiteral("Artists/A/Old/cover.jpg"), QByteArray("cover"));
        auto op = env.storage->begin({trash(QStringLiteral("Artists/A/Old"))});
        QVERIFY(op.ok());
        QVERIFY(op.value()->execute().ok());
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/A/Old/01.mp3"))));  // not yet: phase B
        QVERIFY(env.trashTree().files.isEmpty());
        QVERIFY(op.value()->finish().ok());
        QVERIFY(!QFileInfo::exists(env.lib(QStringLiteral("Artists/A/Old"))));
        QCOMPARE(env.trashTree().files.value(QStringLiteral("Old/01.mp3")), QByteArray("one"));
        QCOMPARE(env.trashTree().files.value(QStringLiteral("Old/cover.jpg")), QByteArray("cover"));
    }

    void trashingAMissingPathIsHarmless()
    {
        StorageEnv env;
        QVERIFY(runAll(env, {trash(QStringLiteral("Artists/Nope"))}).ok());
        QVERIFY(env.trashTree().files.isEmpty());
    }

    void rollbackBeforeTheCommitUndoesPhaseAAndNeverTrashes()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Artists/A/a.mp3"), QByteArray("a"));
        env.writeLib(QStringLiteral("Artists/A/gone.mp3"), QByteArray("g"));
        env.write(env.source(QStringLiteral("n.mp3")), QByteArray("n"));
        const TreeSnapshot before = env.libraryTree();
        auto op = env.storage->begin({moveFile(QStringLiteral("Artists/A/a.mp3"), QStringLiteral("Playlists/P/a.mp3")),
                                      copy(env.source(QStringLiteral("n.mp3")), QStringLiteral("Artists/A/Alb/n.mp3")),
                                      trash(QStringLiteral("Artists/A/gone.mp3"))});
        QVERIFY(op.ok());
        QVERIFY(op.value()->execute().ok());
        QVERIFY(op.value()->rollback().ok());  // e.g. the database part failed
        QVERIFY2(env.libraryTree() == before, qPrintable(env.libraryTree().describe()));
        QVERIFY(env.trashTree().files.isEmpty());
        QCOMPARE(env.journalEntries(), 0);
    }

    void aFailingRecycleBinKeepsTheEntryAndTheNextStartRetries()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Artists/A/x.mp3"), QByteArray("x"));
        auto op = env.storage->begin({trash(QStringLiteral("Artists/A/x.mp3"))});
        QVERIFY(op.ok());
        QVERIFY(op.value()->execute().ok());
        env.trash.failNext();
        QVERIFY(!op.value()->finish().ok());
        QCOMPARE(env.journalEntries(), 1);
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/A/x.mp3"))));  // nothing lost

        const QString id = op.value()->id();
        const auto report = env.storage->recover([&](const QString& opId) -> Result<bool> { return opId == id; });
        QVERIFY(report.ok());
        QCOMPARE(report.value().rolledForward, 1);
        QCOMPARE(env.journalEntries(), 0);
        QCOMPARE(env.trashTree().files.value(QStringLiteral("x.mp3")), QByteArray("x"));
    }

    // ================================================================ one-shot apply
    void applyRunsEverythingInOrderAndCleansTheJournal()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Artists/A/Old/01 - a.mp3"), QByteArray("a"));
        env.writeLib(QStringLiteral("Artists/A/Old/02 - b.mp3"), QByteArray("b"));
        QVERIFY(env.storage->apply({ensureDir(QStringLiteral("Playlists/New")),
                                    moveFile(QStringLiteral("Artists/A/Old/02 - b.mp3"), QStringLiteral("Playlists/New/b.mp3")),
                                    trash(QStringLiteral("Artists/A/Old"))})
                    .ok());
        QCOMPARE(env.read(env.lib(QStringLiteral("Playlists/New/b.mp3"))), QByteArray("b"));
        QVERIFY(!QFileInfo::exists(env.lib(QStringLiteral("Artists/A/Old"))));
        QCOMPARE(env.trashTree().files.value(QStringLiteral("Old/01 - a.mp3")), QByteArray("a"));
        QCOMPARE(env.journalEntries(), 0);
        QVERIFY(env.storage->apply({}).ok());  // nothing to do
    }

    // ================================================================ real Recycle Bin (manual)
    // Off by default so test runs do not fill your Recycle Bin. To check the real thing, set the
    // environment variable SEMPLICE_TEST_REAL_TRASH=1 and run this test: a file called
    // "semplice-trash-test-....txt" must appear in the Windows Recycle Bin (and be restorable).
    void theRealRecycleBin()
    {
        if (qEnvironmentVariable("SEMPLICE_TEST_REAL_TRASH") != QLatin1String("1"))
            QSKIP("set SEMPLICE_TEST_REAL_TRASH=1 to try the real Recycle Bin");
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("semplice-trash-test-%1.txt").arg(QDateTime::currentSecsSinceEpoch()));
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("Semplice Recycle Bin test - you can delete this");
        f.close();
        QtTrash real;
        QVERIFY(real.moveToTrash(file).ok());
        QVERIFY(!QFileInfo::exists(file));
    }

    // ================================================================ OwnFiles
    void ownFilesRefusesFoldersAndNonEmptyFoldersAndRelativePaths()
    {
        StorageEnv env;
        env.writeLib(QStringLiteral("Artists/A/x.mp3"), QByteArray("x"));
        QCOMPARE(ownfiles::removeFile(env.lib(QStringLiteral("Artists/A"))).error().code, ErrorCode::NotAllowed);
        QCOMPARE(ownfiles::removeEmptyDir(env.lib(QStringLiteral("Artists/A"))).error().code, ErrorCode::NotAllowed);
        QCOMPARE(ownfiles::removeFile(QStringLiteral("relative.txt")).error().code, ErrorCode::InvalidArgument);
        QVERIFY(ownfiles::removeFile(env.lib(QStringLiteral("Artists/A/missing.mp3"))).ok());  // missing = fine
        QVERIFY(QFileInfo::exists(env.lib(QStringLiteral("Artists/A/x.mp3"))));
    }
};

QTEST_GUILESS_MAIN(TstFileStorage)
#include "tst_file_storage.moc"
