#pragma once

#include "core/util/Result.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariantList>

#include <memory>

namespace semplice {

// One SQLite connection (Qt SQL, driver QSQLITE). Opens with foreign keys ON and WAL.
// Not thread-safe: use one Database per thread (SPEC 3, TECH_DESIGN 4).
class Database {
public:
    // path = file path or ":memory:". Creates the file if missing. Does NOT migrate (see Migrator).
    // readOnly: for inspecting backups; the file is not modified (no WAL switch, no writes).
    static Result<std::unique_ptr<Database>> open(const QString& path, bool readOnly = false);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Executes a statement with positional arguments. Returns the finished query for reading rows.
    Result<QSqlQuery> run(const QString& sql, const QVariantList& args = {});
    // Same, but only reports success/failure.
    Result<void> exec(const QString& sql, const QVariantList& args = {});

    // Convenience for single-value queries ("SELECT COUNT(*) ..."). Invalid QVariant if no row.
    Result<QVariant> scalar(const QString& sql, const QVariantList& args = {});

    // The raw Qt connection, for tools that reuse prepared statements (generator, benchmarks).
    QSqlDatabase qt() const { return QSqlDatabase::database(m_connectionName, false); }

    qint64 lastInsertId() const { return m_lastInsertId; }
    int changes() const { return m_changes; }
    QString path() const { return m_path; }

    Result<void> begin();
    Result<void> commit();
    Result<void> rollback();
    bool inTransaction() const { return m_depth > 0; }

private:
    Database() = default;

    QString m_connectionName;
    QString m_path;
    qint64 m_lastInsertId = 0;
    int m_changes = 0;
    int m_depth = 0;
};

// Rolls back on destruction unless commit() succeeded. Nested use is not supported:
// services open the transaction, repositories never do.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    Result<void> begin();
    Result<void> commit();
    void rollback();

private:
    Database& m_db;
    bool m_active = false;
};

}  // namespace semplice
