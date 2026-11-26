#include "core/db/Database.h"

#include <QAtomicInt>
#include <QSqlError>

namespace semplice {

namespace {

QAtomicInt g_connectionCounter;

Error dbError(const QString& what, const QSqlError& e)
{
    // A violated UNIQUE / PRIMARY KEY constraint means "this name or key already exists".
    const bool duplicate = e.text().contains(QLatin1String("UNIQUE constraint failed"));
    return Error::make(duplicate ? ErrorCode::AlreadyExists : ErrorCode::DatabaseError,
                       what + QStringLiteral(": ") + e.text());
}

}  // namespace

Result<std::unique_ptr<Database>> Database::open(const QString& path, bool readOnly)
{
    std::unique_ptr<Database> db(new Database());
    db->m_path = path;
    db->m_connectionName = QStringLiteral("semplice-%1").arg(g_connectionCounter.fetchAndAddRelaxed(1));

    QSqlDatabase qdb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), db->m_connectionName);
    qdb.setDatabaseName(path);
    if (readOnly)
        qdb.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!qdb.open())
        return dbError(QStringLiteral("open ") + path, qdb.lastError());

    if (readOnly)
        return db;

    for (const char* pragma : {"PRAGMA foreign_keys = ON", "PRAGMA journal_mode = WAL",
                               "PRAGMA synchronous = NORMAL", "PRAGMA busy_timeout = 5000"}) {
        if (auto r = db->exec(QLatin1String(pragma)); !r.ok())
            return r.error();
    }
    return db;
}

Database::~Database()
{
    {
        QSqlDatabase qdb = QSqlDatabase::database(m_connectionName, false);
        if (qdb.isValid() && qdb.isOpen())
            qdb.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

Result<QSqlQuery> Database::run(const QString& sql, const QVariantList& args)
{
    QSqlDatabase qdb = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(qdb);
    if (!query.prepare(sql))
        return dbError(QStringLiteral("prepare: ") + sql, query.lastError());
    for (const QVariant& a : args)
        query.addBindValue(a);
    if (!query.exec())
        return dbError(QStringLiteral("exec: ") + sql, query.lastError());
    m_lastInsertId = query.lastInsertId().toLongLong();
    m_changes = query.numRowsAffected();
    return query;
}

Result<void> Database::exec(const QString& sql, const QVariantList& args)
{
    auto r = run(sql, args);
    if (!r.ok())
        return r.error();
    return {};
}

Result<QVariant> Database::scalar(const QString& sql, const QVariantList& args)
{
    auto r = run(sql, args);
    if (!r.ok())
        return r.error();
    QSqlQuery q = std::move(r).value();
    return q.next() ? q.value(0) : QVariant();
}

Result<void> Database::begin()
{
    auto r = exec(QStringLiteral("BEGIN IMMEDIATE"));
    if (r.ok())
        ++m_depth;
    return r;
}

Result<void> Database::commit()
{
    auto r = exec(QStringLiteral("COMMIT"));
    if (r.ok())
        m_depth = 0;
    return r;
}

Result<void> Database::rollback()
{
    auto r = exec(QStringLiteral("ROLLBACK"));
    m_depth = 0;
    return r;
}

Transaction::Transaction(Database& db) : m_db(db) {}

Transaction::~Transaction()
{
    if (m_active)
        (void)m_db.rollback();
}

Result<void> Transaction::begin()
{
    auto r = m_db.begin();
    m_active = r.ok();
    return r;
}

Result<void> Transaction::commit()
{
    auto r = m_db.commit();
    if (r.ok())
        m_active = false;
    return r;
}

void Transaction::rollback()
{
    if (m_active) {
        (void)m_db.rollback();
        m_active = false;
    }
}

}  // namespace semplice
