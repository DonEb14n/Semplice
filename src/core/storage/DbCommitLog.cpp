#include "core/storage/DbCommitLog.h"

namespace semplice {

Result<void> DbCommitLog::mark(Database& db, const QString& opId, qint64 nowMs)
{
    return db.exec(QStringLiteral("INSERT OR REPLACE INTO op_log(op_id, committed_at) VALUES(?,?)"), {opId, nowMs});
}

Result<bool> DbCommitLog::isCommitted(Database& db, const QString& opId)
{
    auto v = db.scalar(QStringLiteral("SELECT COUNT(*) FROM op_log WHERE op_id = ?"), {opId});
    if (!v.ok())
        return v.error();
    return v.value().toInt() > 0;
}

Result<void> DbCommitLog::forget(Database& db, const QString& opId)
{
    return db.exec(QStringLiteral("DELETE FROM op_log WHERE op_id = ?"), {opId});
}

Result<void> DbCommitLog::purgeExcept(Database& db, const QStringList& keepIds)
{
    auto rows = db.run(QStringLiteral("SELECT op_id FROM op_log"));
    if (!rows.ok())
        return rows.error();
    QSqlQuery q = std::move(rows).value();
    QStringList stale;
    while (q.next()) {
        if (!keepIds.contains(q.value(0).toString()))
            stale << q.value(0).toString();
    }
    for (const QString& id : std::as_const(stale)) {
        if (auto r = forget(db, id); !r.ok())
            return r.error();
    }
    return {};
}

}  // namespace semplice
