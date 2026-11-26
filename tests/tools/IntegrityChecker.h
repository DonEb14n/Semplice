#pragma once

#include "core/db/Database.h"

#include <QStringList>

namespace semplice::testing {

// Verifies the invariants of SPEC 5 directly in SQL, independent of LibraryService.
// Returns one human-readable line per violation; an empty list means the library is consistent.
QStringList checkLibraryIntegrity(Database& db);

}  // namespace semplice::testing
