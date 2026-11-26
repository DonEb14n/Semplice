#include "core/db/Migrator.h"

#include <QDir>
#include <QFile>

namespace semplice {

namespace {

// Migrations are embedded as resources: ":/semplice/db/0001_init.sql", ...
QStringList migrationFiles()
{
    QDir dir(QStringLiteral(":/semplice/db"));
    return dir.entryList({QStringLiteral("[0-9][0-9][0-9][0-9]_*.sql")}, QDir::Files, QDir::Name);
}

int versionOf(const QString& fileName)
{
    return fileName.left(4).toInt();
}

QStringList splitStatements(const QString& sql)
{
    // Drop "--" comment lines, then split on ';' (no migration contains ';' inside a literal).
    QString cleaned;
    for (const QString& line : sql.split(QLatin1Char('\n'))) {
        if (!line.trimmed().startsWith(QLatin1String("--")))
            cleaned += line + QLatin1Char('\n');
    }
    QStringList out;
    for (const QString& stmt : cleaned.split(QLatin1Char(';'))) {
        if (!stmt.trimmed().isEmpty())
            out << stmt.trimmed();
    }
    return out;
}

}  // namespace

int Migrator::latestVersion()
{
    const QStringList files = migrationFiles();
    return files.isEmpty() ? 0 : versionOf(files.last());
}

Result<int> Migrator::currentVersion(Database& db)
{
    auto v = db.scalar(QStringLiteral("PRAGMA user_version"));
    if (!v.ok())
        return v.error();
    return v.value().toInt();
}

Result<void> Migrator::migrate(Database& db)
{
    auto current = currentVersion(db);
    if (!current.ok())
        return current.error();
    if (current.value() > latestVersion())
        return Error::make(ErrorCode::SchemaTooNew, QStringLiteral("database schema %1 is newer than %2")
                                                        .arg(current.value())
                                                        .arg(latestVersion()));

    for (const QString& file : migrationFiles()) {
        const int version = versionOf(file);
        if (version <= current.value())
            continue;

        QFile f(QStringLiteral(":/semplice/db/") + file);
        if (!f.open(QIODevice::ReadOnly))
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot read migration ") + file);
        const QStringList statements = splitStatements(QString::fromUtf8(f.readAll()));

        Transaction tx(db);
        if (auto r = tx.begin(); !r.ok())
            return r.error();
        for (const QString& stmt : statements) {
            if (auto r = db.exec(stmt); !r.ok())
                return r.error();
        }
        // PRAGMA does not accept bound parameters.
        if (auto r = db.exec(QStringLiteral("PRAGMA user_version = %1").arg(version)); !r.ok())
            return r.error();
        if (auto r = tx.commit(); !r.ok())
            return r.error();
    }
    return {};
}

}  // namespace semplice
