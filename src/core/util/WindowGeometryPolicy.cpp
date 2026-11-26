#include "core/util/WindowGeometryPolicy.h"

#include <algorithm>

namespace semplice {

namespace {

// The window counts as reachable if at least this much of it lies on some screen.
constexpr int kMinVisibleWidth = 100;
constexpr int kMinVisibleHeight = 50;

QRect centredDefault(const QRect& screen, const QSize& size)
{
    QSize s = size;
    if (!screen.isEmpty()) {
        s.setWidth(std::min(s.width(), screen.width()));
        s.setHeight(std::min(s.height(), screen.height()));
    }
    QRect r(QPoint(0, 0), s);
    if (!screen.isEmpty())
        r.moveCenter(screen.center());
    return r;
}

}  // namespace

QRect resolveWindowRect(const WindowGeometry& saved, const QList<QRect>& screens, const QSize& minSize,
                        const QSize& defaultSize)
{
    const QRect primary = screens.isEmpty() ? QRect() : screens.first();

    if (!saved.valid)
        return centredDefault(primary, defaultSize);

    const QRect wanted(saved.x, saved.y, std::max(saved.width, minSize.width()),
                       std::max(saved.height, minSize.height()));

    for (const QRect& screen : screens) {
        const QRect visible = wanted.intersected(screen);
        if (visible.width() >= kMinVisibleWidth && visible.height() >= kMinVisibleHeight)
            return wanted;
    }
    return centredDefault(primary, defaultSize);
}

}  // namespace semplice
