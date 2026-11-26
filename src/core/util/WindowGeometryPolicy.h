#pragma once

#include "core/util/WindowGeometry.h"

#include <QList>
#include <QRect>
#include <QSize>

namespace semplice {

// Decides where the main window opens (SPEC 8.1):
//  - nothing saved            -> default size, centred on the first (primary) screen
//  - saved rect not visible   -> same as above (e.g. the monitor was unplugged)
//  - saved size below minimum -> raised to the minimum
// `screens` are the available geometries of all screens, primary first.
QRect resolveWindowRect(const WindowGeometry& saved, const QList<QRect>& screens, const QSize& minSize,
                        const QSize& defaultSize);

}  // namespace semplice
