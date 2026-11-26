#pragma once

#include <QString>

namespace semplice::platform {

// Windows names the program in the volume panel (System Media Transport Controls) from a Start menu shortcut that
// carries the program's AppUserModelID. The installer makes that shortcut; a program run from an unpacked folder
// has none, so it makes one for itself in the user's own Start menu: "Semplice.lnk" (target = this exe, the exe's
// icon, the id). An existing shortcut that already points at this exe is left alone. Returns false on failure.
bool ensureStartMenuShortcut(const QString& exePath, const QString& appUserModelId, const QString& displayName);

}  // namespace semplice::platform
