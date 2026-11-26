#pragma once

#include "core/storage/FaultInjector.h"
#include "core/storage/IFileStorage.h"
#include "core/storage/ITrash.h"
#include "core/storage/OperationJournal.h"
#include "core/tags/IFileReleaser.h"
#include "core/util/Clock.h"

#include <QByteArray>

#include <functional>
#include <memory>
#include <stop_token>

namespace semplice {

class FileStorageService;

struct RecoveryReport {
    int rolledForward = 0;
    int rolledBack = 0;
    QStringList problems;       // things that could not be repaired (also logged)
    QStringList committedIds;   // two-phase operations completed forward: forget their op_log rows

    bool repairedAnything() const { return rolledForward + rolledBack > 0; }
};

// "Did the database transaction of operation <id> commit?"
using CommitOracle = std::function<Result<bool>(const QString& operationId)>;

// A begun operation: its journal entry exists on disk until finish() or rollback().
class JournaledOperation final : public IStorageOperation {
public:
    ~JournaledOperation() override = default;

    QString id() const override { return m_entry.id; }
    Result<QList<CopyResult>> execute(const ExecuteOptions& options = {}) override;
    Result<void> finish() override;
    Result<void> rollback() override;

private:
    friend class FileStorageService;
    JournaledOperation(FileStorageService& service, JournalEntry entry, qint64 totalBytes)
        : m_service(service), m_entry(std::move(entry)), m_totalBytes(totalBytes) {}

    FileStorageService& m_service;
    JournalEntry m_entry;
    qint64 m_totalBytes = 0;
    int m_started = 0;  // number of phase-A steps begun (the last one may be partial)
};

// The only class that changes files inside the library (SPEC 4.4, CLAUDE.md filesystem rules).
// Paths in FileOps are library-relative; Copy sources are absolute and are only ever read.
class FileStorageService final : public IFileStorage {
public:
    FileStorageService(QString libraryRoot, QString journalDir, ITrash& trash, const IClock& clock);

    // IFileStorage: journaled one-shot, effects are final (no database involved in the decision).
    Result<void> apply(const QList<FileOp>& ops) override;
    Result<void> checkpoint(const QString& name) override;

    // Two-phase API, see JournaledOperation.
    Result<std::unique_ptr<IStorageOperation>> begin(const QList<FileOp>& ops, const QString& description = {}) override;

    // Startup recovery (before the UI is shown). Safe to run more than once.
    Result<RecoveryReport> recover(const CommitOracle& isCommitted);

    // Ids of journal entries that still exist (after recovery: ones that could not be completed yet).
    QStringList pendingOperationIds() const;

    QString rootPath() const { return m_root; }
    QString absolutePath(const QString& relativePath) const;

    // Test hooks.
    void setFaultInjector(FaultInjector* injector) { m_fault = injector; }
    // The player holds the file it plays open, and Windows then refuses to rename or delete it: before a step touches
    // a path the releaser lets go of it (the player pauses and remembers its place), afterwards it may take it back.
    void setReleaser(IFileReleaser* releaser) { m_releaser = releaser; }
    void setChunkSize(qint64 bytes) { m_chunkSize = bytes; }
    // (checkpoint() above is also what harnesses call between execute() and finish() to mark the DB steps.)

private:
    friend class JournaledOperation;

    Result<JournalEntry> prepare(const QList<FileOp>& ops, bool autoCommit, const QString& description,
                                 qint64* totalBytes);
    Result<QString> validateRelative(const QString& relative) const;

    // One step, idempotent where it can be (used by execute, rollback and recovery).
    struct StepContext;  // progress / cancel / results of the running phase A
    Result<void> doStep(const JournalOp& op, StepContext& ctx);
    Result<void> redoStep(const JournalOp& op);
    Result<void> undoStep(const JournalOp& op);
    Result<void> moveForward(const JournalOp& op);
    Result<void> moveBack(const JournalOp& op);
    Result<void> removeCreatedDirs(const JournalOp& op);

    Result<void> rollbackEntry(const JournalEntry& entry, int stepsStarted);
    Result<QList<CopyResult>> executeEntry(JournalEntry& entry, qint64 totalBytes, const ExecuteOptions& options, int& started);
    Result<void> finishEntry(const JournalEntry& entry);

    QString m_root;
    OperationJournal m_journal;
    ITrash& m_trash;
    const IClock& m_clock;
    FaultInjector* m_fault = nullptr;
    IFileReleaser* m_releaser = nullptr;
    qint64 m_chunkSize = 1024 * 1024;
};

}  // namespace semplice
