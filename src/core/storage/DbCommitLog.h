#pragma once

#include "core/db/Database.h"

namespace semplice {

// Table `op_log` (migration 0002). The DB transaction of a journaled operation calls mark() so the
// journal recovery can tell "committed" from "not committed" exactly (SPEC 4.5).
class DbCommitLog {
public:
    // Call INSIDE the operation's transaction, right before COMMIT.
    static Result<void> mark(Database& db, const QString& opId, qint64 nowMs);
    static Result<bool> isCommitted(Database& db, const QString& opId);
    // After the operation has finished or recovery completed it.
    static Result<void> forget(Database& db, const QString& opId);
    // Drops every row except those of operations whose journal entry still exists.
    static Result<void> purgeExcept(Database& db, const QStringList& keepIds);
};

}  // namespace semplice
