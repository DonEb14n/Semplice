#pragma once

#include "core/util/Result.h"

namespace semplice::ownfiles {

// The ONLY place in the code base that removes files or folders directly (SPEC 4.4).
// A CTest ("no_direct_deletes") fails if any other source file calls a delete API.
//
// Library content (audio, covers) never goes through here: it leaves the library via ITrash.
// These helpers are for Semplice's own housekeeping: journal entries, old backups, log files,
// and files that an *uncommitted* operation created itself (a cancelled import's copies).

Result<void> removeFile(const QString& absolutePath);          // missing file = success
Result<void> removeEmptyDir(const QString& absolutePath);      // refuses (returns NotAllowed) if not empty

}  // namespace semplice::ownfiles
