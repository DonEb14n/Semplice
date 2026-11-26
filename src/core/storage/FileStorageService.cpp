#include "core/storage/FileStorageService.h"

#include "core/storage/OwnFiles.h"
#include "core/storage/PathSanitizer.h"
#include "core/util/Logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace semplice {

#define SEMPLICE_TRY(expr)                      \
    do {                                        \
        auto semplice_try_result = (expr);      \
        if (!semplice_try_result.ok())          \
            return semplice_try_result.error(); \
    } while (false)

#define SEMPLICE_ASSIGN_REL(name, expr)            \
    auto name##_result = validateRelative(expr);   \
    if (!name##_result.ok())                       \
        return name##_result.error();              \
    const QString name = name##_result.value()

struct FileStorageService::StepContext {
    int opIndex = 0;
    int copyIndex = 0;
    int copyCount = 0;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    const ExecuteOptions* options = nullptr;
    QList<CopyResult>* results = nullptr;
};

namespace {

const QString kDotLibrary = QStringLiteral(".library");

QString parentOf(const QString& relative)
{
    const qsizetype slash = relative.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : relative.left(slash);
}

// NTFS is case-insensitive: QFileInfo::exists() cannot tell "abba" from "ABBA". Look at the real name.
bool existsExact(const QString& absolute)
{
    const QFileInfo info(absolute);
    if (!info.exists() && !info.isSymLink())
        return false;
    const QStringList names =
        info.dir().entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    return names.contains(info.fileName());
}

bool existsAny(const QString& absolute)
{
    const QFileInfo info(absolute);
    return info.exists() || info.isSymLink();
}

}  // namespace

FileStorageService::FileStorageService(QString libraryRoot, QString journalDir, ITrash& trash, const IClock& clock)
    : m_root(QDir::cleanPath(libraryRoot)), m_journal(std::move(journalDir)), m_trash(trash), m_clock(clock)
{
}

QString FileStorageService::absolutePath(const QString& relativePath) const
{
    return QDir(m_root).filePath(relativePath);
}

QStringList FileStorageService::pendingOperationIds() const
{
    QStringList ids;
    auto pending = m_journal.pending();
    if (pending.ok()) {
        for (const JournalEntry& e : pending.value())
            ids << e.id;
    }
    return ids;
}

Result<void> FileStorageService::checkpoint(const QString& name)
{
    if (m_fault && m_fault->crashHere(name))
        return Error::make(ErrorCode::CrashInjected, name);
    return {};
}

Result<QString> FileStorageService::validateRelative(const QString& relative) const
{
    const QString cleaned = QDir::cleanPath(relative);
    if (cleaned.isEmpty() || cleaned == QLatin1String(".") || QDir::isAbsolutePath(cleaned) ||
        cleaned.contains(QLatin1Char(':')) || cleaned == QLatin1String("..") || cleaned.startsWith(QLatin1String("../")) ||
        cleaned.contains(QLatin1String("/../")))
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("unsafe library path: ") + relative);
    const QString first = cleaned.section(QLatin1Char('/'), 0, 0);
    if (first.compare(kDotLibrary, Qt::CaseInsensitive) == 0)
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("operations may not touch .library: ") + relative);
    SEMPLICE_TRY(PathSanitizer::checkPathLength(absolutePath(cleaned)));
    return cleaned;
}

// ---------------------------------------------------------------------------------------------
// Preparing an entry
// ---------------------------------------------------------------------------------------------

Result<JournalEntry> FileStorageService::prepare(const QList<FileOp>& ops, bool autoCommit, const QString& description,
                                                 qint64* totalBytes)
{
    JournalEntry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.description = description;
    entry.autoCommit = autoCommit;
    entry.createdAtMs = m_clock.now().toMSecsSinceEpoch();

    QSet<QString> planned;  // folders that earlier steps of this entry will have created
    auto missingDirs = [&](const QString& dirRelative) {
        QStringList created;
        QString current;
        for (const QString& part : dirRelative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            current = current.isEmpty() ? part : current + QLatin1Char('/') + part;
            const QString key = PathSanitizer::compareKey(current);
            if (!QFileInfo(absolutePath(current)).exists() && !planned.contains(key)) {
                created << current;
                planned.insert(key);
            }
        }
        return created;
    };

    qint64 bytes = 0;
    for (const FileOp& op : ops) {
        JournalOp j;
        j.kind = op.kind;
        switch (op.kind) {
        case FileOp::Kind::EnsureDir: {
            SEMPLICE_ASSIGN_REL(from, op.from);
            j.from = from;
            j.createdDirs = missingDirs(from);
            break;
        }
        case FileOp::Kind::Copy: {
            SEMPLICE_ASSIGN_REL(to, op.to);
            const QFileInfo src(op.from);
            if (!src.isAbsolute() || !src.isFile())
                return Error::make(ErrorCode::NotFound, QStringLiteral("copy source is not a file: ") + op.from);
            j.from = op.from;
            j.to = to;
            j.dstPreexisting = existsAny(absolutePath(to));
            j.createdDirs = missingDirs(parentOf(to));
            bytes += src.size();
            break;
        }
        case FileOp::Kind::MoveFile:
        case FileOp::Kind::MoveDir: {
            SEMPLICE_ASSIGN_REL(from, op.from);
            SEMPLICE_ASSIGN_REL(to, op.to);
            j.from = from;
            j.to = to;
            const bool caseOnly = from != to && PathSanitizer::compareKey(from) == PathSanitizer::compareKey(to);
            if (caseOnly) {
                j.via = parentOf(to) + (parentOf(to).isEmpty() ? QString() : QStringLiteral("/")) +
                        QStringLiteral(".semplice-tmp-") + entry.id.left(8);
            } else {
                j.dstPreexisting = existsAny(absolutePath(to));
                j.createdDirs = missingDirs(parentOf(to));
            }
            break;
        }
        case FileOp::Kind::Trash: {
            SEMPLICE_ASSIGN_REL(from, op.from);
            j.from = from;
            break;
        }
        }
        entry.ops.append(j);
    }
    if (totalBytes)
        *totalBytes = bytes;
    return entry;
}

Result<std::unique_ptr<IStorageOperation>> FileStorageService::begin(const QList<FileOp>& ops,
                                                                       const QString& description)
{
    qint64 totalBytes = 0;
    auto entry = prepare(ops, false, description, &totalBytes);
    if (!entry.ok())
        return entry.error();
    SEMPLICE_TRY(m_journal.write(entry.value()));
    SEMPLICE_TRY(checkpoint(QStringLiteral("journal:written")));
    return std::unique_ptr<IStorageOperation>(new JournaledOperation(*this, std::move(entry).value(), totalBytes));
}

Result<void> FileStorageService::apply(const QList<FileOp>& ops)
{
    if (ops.isEmpty())
        return {};
    qint64 totalBytes = 0;
    auto entry = prepare(ops, true, QStringLiteral("apply"), &totalBytes);
    if (!entry.ok())
        return entry.error();
    SEMPLICE_TRY(m_journal.write(entry.value()));
    SEMPLICE_TRY(checkpoint(QStringLiteral("journal:written")));
    JournaledOperation op(*this, std::move(entry).value(), totalBytes);
    auto executed = op.execute();
    if (!executed.ok())
        return executed.error();
    return op.finish();
}

// ---------------------------------------------------------------------------------------------
// Single steps
// ---------------------------------------------------------------------------------------------

namespace {
// Lets go of a path for the time of one step (also when the step fails).
struct ReleaseScope {
    ReleaseScope(IFileReleaser* releaser, QString path) : m_releaser(releaser), m_path(std::move(path))
    {
        if (m_releaser)
            (void)m_releaser->releaseFile(m_path);
    }
    ~ReleaseScope()
    {
        if (m_releaser)
            m_releaser->fileReady(m_path);
    }
    IFileReleaser* m_releaser;
    QString m_path;
};
}  // namespace

Result<void> FileStorageService::moveForward(const JournalOp& op)
{
    ReleaseScope release(m_releaser, absolutePath(op.from));
    const bool isDir = op.kind == FileOp::Kind::MoveDir;
    auto rename = [&](const QString& a, const QString& b) -> bool {
        return isDir ? QDir().rename(a, b) : QFile::rename(a, b);
    };
    const QString from = absolutePath(op.from);
    const QString to = absolutePath(op.to);

    if (!op.via.isEmpty()) {  // case-only rename: from -> via -> to, resumable from any point
        const QString via = absolutePath(op.via);
        if (existsExact(from)) {
            if (!rename(from, via))
                return Error::make(ErrorCode::IoError, QStringLiteral("cannot rename ") + from);
        }
        if (existsExact(via)) {
            if (!rename(via, to))
                return Error::make(ErrorCode::IoError, QStringLiteral("cannot rename ") + via);
            return {};
        }
        if (existsExact(to))
            return {};  // already done
        return Error::make(ErrorCode::NotFound, QStringLiteral("nothing to rename: ") + from);
    }

    if (!existsAny(from)) {
        if (existsAny(to))
            return {};  // already done
        return Error::make(ErrorCode::NotFound, QStringLiteral("missing: ") + from);
    }
    if (existsAny(to))
        return Error::make(ErrorCode::AlreadyExists, QStringLiteral("destination exists: ") + to);
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (!rename(from, to)) {
        const bool tooLong = to.size() > 259;
        return Error::make(tooLong ? ErrorCode::PathTooLong : ErrorCode::IoError,
                           QStringLiteral("cannot move %1 to %2").arg(from, to));
    }
    return {};
}

Result<void> FileStorageService::removeCreatedDirs(const JournalOp& op)
{
    for (auto it = op.createdDirs.crbegin(); it != op.createdDirs.crend(); ++it) {
        auto r = ownfiles::removeEmptyDir(absolutePath(*it));
        if (!r.ok() && r.error().code != ErrorCode::NotAllowed)  // not empty = someone else uses it: keep
            return r.error();
    }
    return {};
}

Result<void> FileStorageService::moveBack(const JournalOp& op)
{
    ReleaseScope release(m_releaser, absolutePath(op.to));
    const bool isDir = op.kind == FileOp::Kind::MoveDir;
    auto rename = [&](const QString& a, const QString& b) -> bool {
        return isDir ? QDir().rename(a, b) : QFile::rename(a, b);
    };
    const QString from = absolutePath(op.from);
    const QString to = absolutePath(op.to);

    if (!op.via.isEmpty()) {
        const QString via = absolutePath(op.via);
        if (existsExact(from))
            return {};  // never moved, or already back
        if (existsExact(via)) {
            if (!rename(via, from))
                return Error::make(ErrorCode::IoError, QStringLiteral("cannot restore ") + from);
            return {};
        }
        if (existsExact(to)) {
            if (!rename(to, via) || !rename(via, from))
                return Error::make(ErrorCode::IoError, QStringLiteral("cannot restore ") + from);
        }
        return {};
    }

    if (existsAny(to) && !existsAny(from)) {
        if (!rename(to, from))
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot restore ") + from);
    }
    return removeCreatedDirs(op);
}

Result<void> FileStorageService::undoStep(const JournalOp& op)
{
    switch (op.kind) {
    case FileOp::Kind::EnsureDir:
        return removeCreatedDirs(op);
    case FileOp::Kind::Copy: {
        // Only a file this operation created: it did not exist when the entry was written.
        const QString dst = absolutePath(op.to);
        if (!op.dstPreexisting && QFileInfo(dst).isFile())
            SEMPLICE_TRY(ownfiles::removeFile(dst));
        return removeCreatedDirs(op);
    }
    case FileOp::Kind::MoveFile:
    case FileOp::Kind::MoveDir:
        return moveBack(op);
    case FileOp::Kind::Trash:
        return {};  // never executed before the commit; cannot be undone afterwards
    }
    return {};
}

Result<void> FileStorageService::redoStep(const JournalOp& op)
{
    switch (op.kind) {
    case FileOp::Kind::EnsureDir:
        QDir().mkpath(absolutePath(op.from));
        return {};
    case FileOp::Kind::Copy: {
        const QString dst = absolutePath(op.to);
        if (!QFileInfo(dst).exists() && QFileInfo(op.from).isFile()) {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (!QFile::copy(op.from, dst))
                return Error::make(ErrorCode::IoError, QStringLiteral("cannot finish copy to ") + dst);
        }
        return {};
    }
    case FileOp::Kind::MoveFile:
    case FileOp::Kind::MoveDir:
        return moveForward(op);
    case FileOp::Kind::Trash: {
        const QString path = absolutePath(op.from);
        if (existsAny(path)) {
            ReleaseScope release(m_releaser, path);
            return m_trash.moveToTrash(path);
        }
        return {};
    }
    }
    return {};
}

Result<void> FileStorageService::doStep(const JournalOp& op, StepContext& ctx)
{
    switch (op.kind) {
    case FileOp::Kind::EnsureDir:
        if (!QDir().mkpath(absolutePath(op.from)))
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot create folder ") + op.from);
        return {};

    case FileOp::Kind::MoveFile:
    case FileOp::Kind::MoveDir:
        return moveForward(op);

    case FileOp::Kind::Trash: {
        const QString path = absolutePath(op.from);
        if (!existsAny(path))
            return {};  // already gone: nothing to do
        ReleaseScope release(m_releaser, path);
        return m_trash.moveToTrash(path);
    }

    case FileOp::Kind::Copy: {
        ++ctx.copyIndex;
        QFile src(op.from);
        if (!src.open(QIODevice::ReadOnly))  // the original is only ever read
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + op.from);
        const QString dstPath = absolutePath(op.to);
        QDir().mkpath(QFileInfo(dstPath).absolutePath());
        QFile dst(dstPath);
        if (!dst.open(QIODevice::WriteOnly | QIODevice::NewOnly))  // never overwrites
            return Error::make(QFileInfo(dstPath).exists() ? ErrorCode::AlreadyExists : ErrorCode::IoError,
                               QStringLiteral("cannot create ") + dstPath);

        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 copied = 0;
        int chunk = 0;
        while (!src.atEnd()) {
            if (ctx.options && ctx.options->cancel.stop_requested())
                return Error::make(ErrorCode::Cancelled);
            const QByteArray buffer = src.read(m_chunkSize);
            if (buffer.isEmpty())
                return Error::make(ErrorCode::IoError, QStringLiteral("read error: ") + op.from);
            hash.addData(buffer);
            if (dst.write(buffer) != buffer.size())
                return Error::make(ErrorCode::IoError, QStringLiteral("write error: ") + dstPath);
            copied += buffer.size();
            ctx.bytesDone += buffer.size();
            if (ctx.options && ctx.options->progress)
                ctx.options->progress({ctx.copyIndex, ctx.copyCount, ctx.bytesDone, ctx.bytesTotal});
            SEMPLICE_TRY(checkpoint(QStringLiteral("op:%1:chunk:%2").arg(ctx.opIndex).arg(++chunk)));
        }
        if (!dst.flush())
            return Error::make(ErrorCode::IoError, QStringLiteral("flush error: ") + dstPath);
        dst.close();
        if (QFileInfo(dstPath).size() != src.size())
            return Error::make(ErrorCode::IoError, QStringLiteral("size mismatch after copy: ") + dstPath);
        if (ctx.results)
            ctx.results->append({ctx.opIndex, hash.result(), copied});
        return {};
    }
    }
    return {};
}

// ---------------------------------------------------------------------------------------------
// JournaledOperation
// ---------------------------------------------------------------------------------------------

Result<QList<CopyResult>> JournaledOperation::execute(const ExecuteOptions& options)
{
    return m_service.executeEntry(m_entry, m_totalBytes, options, m_started);
}

Result<QList<CopyResult>> FileStorageService::executeEntry(JournalEntry& entry, qint64 totalBytes,
                                                           const ExecuteOptions& options, int& started)
{
    QList<CopyResult> results;
    StepContext ctx;
    ctx.options = &options;
    ctx.results = &results;
    ctx.bytesTotal = totalBytes;
    for (const JournalOp& op : std::as_const(entry.ops)) {
        if (op.kind == FileOp::Kind::Copy)
            ++ctx.copyCount;
    }

    for (int i = 0; i < entry.ops.size(); ++i) {
        const JournalOp& op = entry.ops.at(i);
        if (!entry.autoCommit && op.kind == FileOp::Kind::Trash)
            continue;  // phase B: only after the database commit
        ctx.opIndex = i;
        started = i + 1;

        SEMPLICE_TRY(checkpoint(QStringLiteral("op:%1:before").arg(i)));
        Result<void> step = options.cancel.stop_requested() ? Result<void>(Error::make(ErrorCode::Cancelled))
                                                            : doStep(op, ctx);
        if (!step.ok()) {
            if (step.error().code == ErrorCode::CrashInjected)
                return step.error();  // power loss: no cleanup at all
            if (!entry.autoCommit)
            {
                auto rb = rollbackEntry(entry, started);  // undo what this operation did
                if (!rb.ok() && rb.error().code == ErrorCode::CrashInjected)
                    return rb.error();
            }
            // autoCommit entries stay in the journal and are completed at the next start
            return step.error();
        }
        SEMPLICE_TRY(checkpoint(QStringLiteral("op:%1:after").arg(i)));
    }
    SEMPLICE_TRY(checkpoint(QStringLiteral("phase-a:done")));
    return results;
}

Result<void> FileStorageService::rollbackEntry(const JournalEntry& entry, int stepsStarted)
{
    SEMPLICE_TRY(checkpoint(QStringLiteral("rollback:start")));
    for (int i = std::min<int>(stepsStarted, static_cast<int>(entry.ops.size())) - 1; i >= 0; --i) {
        SEMPLICE_TRY(undoStep(entry.ops.at(i)));
        SEMPLICE_TRY(checkpoint(QStringLiteral("rollback:%1").arg(i)));
    }
    SEMPLICE_TRY(m_journal.remove(entry.id));
    return checkpoint(QStringLiteral("rollback:done"));
}

Result<void> JournaledOperation::rollback()
{
    return m_service.rollbackEntry(m_entry, m_entry.ops.size());
}

Result<void> JournaledOperation::finish()
{
    return m_service.finishEntry(m_entry);
}

Result<void> FileStorageService::finishEntry(const JournalEntry& entry)
{
    if (!entry.autoCommit) {
        for (int i = 0; i < entry.ops.size(); ++i) {
            const JournalOp& op = entry.ops.at(i);
            if (op.kind != FileOp::Kind::Trash)
                continue;
            SEMPLICE_TRY(checkpoint(QStringLiteral("trash:%1:before").arg(i)));
            const QString path = absolutePath(op.from);
            if (existsAny(path)) {
                ReleaseScope release(m_releaser, path);
                auto r = m_trash.moveToTrash(path);
                if (!r.ok())
                    return r.error();  // the entry stays; the next start retries
            }
            SEMPLICE_TRY(checkpoint(QStringLiteral("trash:%1:after").arg(i)));
        }
    }
    SEMPLICE_TRY(checkpoint(QStringLiteral("journal:before-delete")));
    SEMPLICE_TRY(m_journal.remove(entry.id));
    return checkpoint(QStringLiteral("journal:deleted"));
}

// ---------------------------------------------------------------------------------------------
// Recovery
// ---------------------------------------------------------------------------------------------

Result<RecoveryReport> FileStorageService::recover(const CommitOracle& isCommitted)
{
    RecoveryReport report;
    QStringList readProblems;
    auto pending = m_journal.pending(&readProblems);
    if (!pending.ok())
        return pending.error();
    report.problems = readProblems;

    for (const JournalEntry& entry : pending.value()) {
        bool committed = entry.autoCommit;
        if (!committed) {
            auto answer = isCommitted(entry.id);
            if (!answer.ok()) {
                report.problems << QStringLiteral("cannot decide operation %1: %2").arg(entry.id, answer.error().devMessage);
                continue;  // leave it for the next start
            }
            committed = answer.value();
        }

        bool keepEntry = false;
        if (committed) {
            // Roll forward: finish what is missing, then the deletions.
            auto note = [&](const Result<void>& r, const JournalOp& op) {
                if (!r.ok())
                    report.problems << QStringLiteral("operation %1: %2").arg(entry.id, r.error().devMessage);
                if (!r.ok() && op.kind == FileOp::Kind::Trash)
                    keepEntry = true;  // trashing failed (locked?): retry next start
            };
            if (entry.autoCommit) {
                for (const JournalOp& op : entry.ops)
                    note(redoStep(op), op);
            } else {
                for (const JournalOp& op : entry.ops) {
                    if (op.kind != FileOp::Kind::Trash)
                        note(redoStep(op), op);
                }
                for (const JournalOp& op : entry.ops) {
                    if (op.kind == FileOp::Kind::Trash)
                        note(redoStep(op), op);
                }
            }
            ++report.rolledForward;
            if (!keepEntry)
                report.committedIds << entry.id;
        } else {
            // Roll back: undo in reverse. Trash steps were never run before the commit.
            for (auto it = entry.ops.crbegin(); it != entry.ops.crend(); ++it) {
                auto r = undoStep(*it);
                if (!r.ok()) {
                    report.problems << QStringLiteral("operation %1: %2").arg(entry.id, r.error().devMessage);
                    keepEntry = true;
                }
            }
            ++report.rolledBack;
        }
        if (!keepEntry) {
            if (auto r = m_journal.remove(entry.id); !r.ok())
                report.problems << r.error().devMessage;
        }
    }
    for (const QString& p : std::as_const(report.problems))
        SLOG_WARN(QStringLiteral("journal recovery: ") + p);
    if (report.repairedAnything())
        SLOG_INFO(QStringLiteral("journal recovery: %1 rolled forward, %2 rolled back")
                      .arg(report.rolledForward)
                      .arg(report.rolledBack));
    return report;
}

}  // namespace semplice
