#pragma once

#include "core/util/Result.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <stop_token>

namespace semplice {

// One filesystem effect of a library operation. Paths are library-relative.
struct FileOp {
    enum class Kind {
        EnsureDir,  // create `from` if missing
        Copy,       // from (absolute path outside the library, never modified) -> to (library-relative)
        MoveFile,   // from -> to
        MoveDir,    // from -> to (whole folder)
        Trash,      // from -> Recycle Bin (file or folder), never a permanent delete
    };
    Kind kind = Kind::EnsureDir;
    QString from;
    QString to;

    bool operator==(const FileOp&) const = default;
};

struct CopyResult {
    int opIndex = 0;       // index of the Copy op in the operation's op list
    QByteArray sha256;     // SHA-256 of the SOURCE bytes (SPEC 6.1), 32 bytes
    qint64 size = 0;
};

struct CopyProgress {
    int fileIndex = 0;     // 1-based, among the Copy ops
    int fileCount = 0;
    qint64 bytesDone = 0;  // over the whole operation
    qint64 bytesTotal = 0;
};

struct ExecuteOptions {
    std::stop_token cancel;                                   // request_stop() cancels and rolls back
    std::function<void(const CopyProgress&)> progress;        // called from the executing thread
};

// A begun file operation (SPEC 4.5). Typical use:
//   auto op = storage.begin(ops);   op->execute();
//   { DB transaction: ...changes..., DbCommitLog::mark(db, op->id(), now), commit }
//   op->finish();                   // or op->rollback() if the DB part failed
class IStorageOperation {
public:
    virtual ~IStorageOperation() = default;
    virtual QString id() const = 0;
    // Phase A: folders, copies, moves. On failure or cancel everything done is undone.
    virtual Result<QList<CopyResult>> execute(const ExecuteOptions& options = {}) = 0;
    // Phase B (only after the DB commit): Recycle Bin, then the journal entry is removed.
    virtual Result<void> finish() = 0;
    // Undo phase A (the DB part failed) and remove the journal entry.
    virtual Result<void> rollback() = 0;
};

// Seam between the library rules and the real, journaled FileStorageService.
class IFileStorage {
public:
    virtual ~IFileStorage() = default;
    // One-shot, journaled, effects are final (no database involved in the decision).
    virtual Result<void> apply(const QList<FileOp>& ops) = 0;
    // Two-phase; see IStorageOperation.
    virtual Result<std::unique_ptr<IStorageOperation>> begin(const QList<FileOp>& ops, const QString& description = {}) = 0;
    // Named step inside a larger operation (the database commit); lets tests simulate a crash there.
    virtual Result<void> checkpoint(const QString& name)
    {
        Q_UNUSED(name);
        return {};
    }
};

// Test double / fake: remembers what it was asked to do and touches no files. begin() records the
// ops when the operation is finished (= after the commit), apply() immediately.
class RecordingFileStorage final : public IFileStorage {
public:
    Result<void> apply(const QList<FileOp>& ops) override
    {
        if (m_failNext) {
            m_failNext = false;
            return Error::make(ErrorCode::IoError, QStringLiteral("simulated failure"));
        }
        m_applied.append(ops);
        return {};
    }

    Result<std::unique_ptr<IStorageOperation>> begin(const QList<FileOp>& ops, const QString& = {}) override
    {
        return std::unique_ptr<IStorageOperation>(new Operation(*this, ops, ++m_counter));
    }

    const QList<FileOp>& applied() const { return m_applied; }
    void clear() { m_applied.clear(); }
    void failNext() { m_failNext = true; }

private:
    class Operation final : public IStorageOperation {
    public:
        Operation(RecordingFileStorage& owner, QList<FileOp> ops, int number)
            : m_owner(owner), m_ops(std::move(ops)), m_id(QStringLiteral("recording-%1").arg(number)) {}
        QString id() const override { return m_id; }
        Result<QList<CopyResult>> execute(const ExecuteOptions& = {}) override { return QList<CopyResult>(); }
        Result<void> finish() override { return m_owner.apply(m_ops); }
        Result<void> rollback() override { return {}; }

    private:
        RecordingFileStorage& m_owner;
        QList<FileOp> m_ops;
        QString m_id;
    };

    QList<FileOp> m_applied;
    bool m_failNext = false;
    int m_counter = 0;
};

}  // namespace semplice
