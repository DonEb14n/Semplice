#pragma once

#include "core/services/ISettings.h"

#include <QSettings>
#include <QStringList>

namespace semplice {

// ISettings backed by an INI file (default: %APPDATA%\Semplice\settings.ini).
class SettingsService final : public ISettings {
public:
    explicit SettingsService(const QString& iniPath);

    static QString defaultPath();
    // Makes the file self-documenting: writes every missing key with its default value.
    void writeMissingDefaults();
    static QStringList supportedLanguages();
    static constexpr int kMaxUserNameLength = 50;

    QString language() const override;
    void setLanguage(const QString& language) override;
    QString userName() const override;
    void setUserName(const QString& name) override;
    QString theme() const override;
    void setTheme(const QString& theme) override;
    double volume() const override;
    void setVolume(double volume) override;
    bool muted() const override;
    void setMuted(bool muted) override;
    QString libraryPath() const override;
    void setLibraryPath(const QString& path) override;
    WindowGeometry windowGeometry() const override;
    void setWindowGeometry(const WindowGeometry& geometry) override;
    QString uiValue(const QString& key, const QString& fallback) const override;
    void setUiValue(const QString& key, const QString& value) override;

private:
    mutable QSettings m_settings;
};

}  // namespace semplice
