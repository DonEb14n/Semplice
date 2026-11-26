#pragma once

#include "core/storage/LibraryFolder.h"

namespace semplice {

struct MaintenanceReport {
    bool optimized = false;
    bool vacuumed = false;
    bool backupCreated = false;
};

// SPEC 6.5 step 3 / 6.7: `PRAGMA optimize`, VACUUM when more than 20 % of the pages are free, backup
// rotation. (Purging thumbnails of missing covers joins in with the thumbnail cache, M6.)
class Maintenance {
public:
    static constexpr double kVacuumFreeRatio = 0.20;
    static Result<MaintenanceReport> run(const LibraryLayout& layout, Database& db, const IClock& clock);
};

}  // namespace semplice
