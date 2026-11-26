#pragma once

#include <QString>

namespace semplice::platform {

// Writes a small text file (crash-<timestamp>.txt) into `logDir` when the process dies from an
// unhandled exception. Windows only; a no-op elsewhere. Minidumps are not written yet.
void installCrashHandler(const QString& logDir, const QString& appVersion);

}  // namespace semplice::platform
