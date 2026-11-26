#pragma once

#include "core/db/Database.h"

namespace semplice {

// Applies numbered SQL migrations (resource files ":/semplice/db/NNNN_*.sql") and tracks them in
// PRAGMA user_version. An applied migration is never edited: add a new one.
class Migrator {
public:
    // Highest schema version this build knows.
    static int latestVersion();

    // Brings `db` up to latestVersion(). Fails with E_SCHEMA_TOO_NEW if the file is newer.
    static Result<void> migrate(Database& db);

    static Result<int> currentVersion(Database& db);
};

}  // namespace semplice
