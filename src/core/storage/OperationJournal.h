#pragma once

#include "core/storage/IFileStorage.h"

#include <QStringList>

namespace semplice {

// One step of a journaled operation, with everything needed to undo or finish it later.
struct JournalOp {
    FileOp::Kind kind = FileOp::Kind::EnsureDir;
    QString from;
    QString to;
    QString via;               // temporary name for case-only renames (abba -> ABBA)
    QStringList createdDirs;   // library-relative folders this step creates, outermost first
    bool dstPreexisting = false;  // `to` already existed when the entry was written: never removed on undo
};

struct JournalEntry {
    QString id;
    QString description;
    // true: the one-shot apply() path. Effects are final, recovery always rolls forward and
    // performs the steps in the listed order. false: two-phase; the commit marker in the database decides.
    bool autoCommit = false;
    qint64 createdAtMs = 0;
    QList<JournalOp> ops;
};

// Files `<journalDir>/<uuid>.json`, written atomically and flushed to disk (SPEC 4.5).
class OperationJournal {
public:
    explicit OperationJournal(QString journalDir) : m_dir(std::move(journalDir)) {}

    Result<void> write(const JournalEntry& entry);
    Result<void> remove(const QString& id);
    // Unreadable entries are skipped and described in `problems`.
    Result<QList<JournalEntry>> pending(QStringList* problems = nullptr) const;

    QString directory() const { return m_dir; }

private:
    QString pathFor(const QString& id) const;
    QString m_dir;
};

}  // namespace semplice
