#pragma once

#include "core/util/WindowGeometry.h"

#include <QString>

namespace semplice {

// App-level settings (not library-bound). Faked in tests.
class ISettings {
public:
    virtual ~ISettings() = default;

    virtual QString language() const = 0;  // "en" | "ru" | "zh_CN" | "ja"
    virtual void setLanguage(const QString& language) = 0;

    virtual QString userName() const = 0;  // empty = show the localised "Listener"
    virtual void setUserName(const QString& name) = 0;

    virtual QString theme() const = 0;
    virtual void setTheme(const QString& theme) = 0;

    virtual double volume() const = 0;  // 0..1, default 0.5
    virtual void setVolume(double volume) = 0;
    virtual bool muted() const = 0;
    virtual void setMuted(bool muted) = 0;

    // Folder of the library that was open last (SPEC 6.6). Empty = none yet.
    virtual QString libraryPath() const = 0;
    virtual void setLibraryPath(const QString& path) = 0;

    // Small UI facts that are not worth their own accessor (last tab, view mode per tab, ...).
    virtual QString uiValue(const QString& key, const QString& fallback) const { Q_UNUSED(key) return fallback; }
    virtual void setUiValue(const QString& key, const QString& value) { Q_UNUSED(key) Q_UNUSED(value) }

    virtual WindowGeometry windowGeometry() const = 0;
    virtual void setWindowGeometry(const WindowGeometry& geometry) = 0;
};

}  // namespace semplice
