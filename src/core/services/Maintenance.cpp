#include "core/services/Maintenance.h"

namespace semplice {

Result<MaintenanceReport> Maintenance::run(const LibraryLayout& layout, Database& db, const IClock& clock)
{
    MaintenanceReport report;
    if (auto r = db.exec(QStringLiteral("PRAGMA optimize")); !r.ok())
        return r.error();
    report.optimized = true;

    auto pages = db.scalar(QStringLiteral("PRAGMA page_count"));
    auto freePages = db.scalar(QStringLiteral("PRAGMA freelist_count"));
    if (pages.ok() && freePages.ok() && pages.value().toLongLong() > 0 &&
        static_cast<double>(freePages.value().toLongLong()) / static_cast<double>(pages.value().toLongLong()) > kVacuumFreeRatio) {
        if (auto r = db.exec(QStringLiteral("VACUUM")); !r.ok())
            return r.error();
        report.vacuumed = true;
    }

    auto backup = LibraryFolder::rotateBackupsIfDue(layout, db, clock);
    if (!backup.ok())
        return backup.error();
    report.backupCreated = backup.value();
    return report;
}

}  // namespace semplice
