#include "core/services/SettingsService.h"

#include <QDir>
#include <QStandardPaths>

#include <algorithm>

namespace semplice {

SettingsService::SettingsService(const QString& iniPath) : m_settings(iniPath, QSettings::IniFormat) {}

QString SettingsService::defaultPath()
{
    QString base = qEnvironmentVariable("APPDATA");  // Windows roaming profile
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath(QStringLiteral("Semplice/settings.ini"));
}

void SettingsService::writeMissingDefaults()
{
    auto ensure = [this](const QString& key, const QVariant& value) {
        if (!m_settings.contains(key))
            m_settings.setValue(key, value);
    };
    ensure(QStringLiteral("app/language"), QStringLiteral("en"));
    ensure(QStringLiteral("app/name"), QString());
    ensure(QStringLiteral("app/theme"), QStringLiteral("true-white"));
    ensure(QStringLiteral("audio/volume"), 0.5);
    ensure(QStringLiteral("audio/muted"), false);
    ensure(QStringLiteral("library/path"), QString());
    m_settings.sync();
}

QStringList SettingsService::supportedLanguages()
{
    return {QStringLiteral("en"), QStringLiteral("ru"), QStringLiteral("zh_CN"), QStringLiteral("ja")};
}

QString SettingsService::language() const
{
    const QString value = m_settings.value(QStringLiteral("app/language")).toString();
    return supportedLanguages().contains(value) ? value : QStringLiteral("en");
}

void SettingsService::setLanguage(const QString& language)
{
    m_settings.setValue(QStringLiteral("app/language"),
                        supportedLanguages().contains(language) ? language : QStringLiteral("en"));
    m_settings.sync();
}

QString SettingsService::userName() const
{
    return m_settings.value(QStringLiteral("app/name")).toString().trimmed().left(kMaxUserNameLength);
}

void SettingsService::setUserName(const QString& name)
{
    m_settings.setValue(QStringLiteral("app/name"), name.trimmed().left(kMaxUserNameLength));
    m_settings.sync();
}

QString SettingsService::theme() const
{
    return m_settings.value(QStringLiteral("app/theme"), QStringLiteral("true-white")).toString();
}

void SettingsService::setTheme(const QString& theme)
{
    m_settings.setValue(QStringLiteral("app/theme"), theme);
    m_settings.sync();
}

double SettingsService::volume() const
{
    bool ok = false;
    const double v = m_settings.value(QStringLiteral("audio/volume"), 0.5).toDouble(&ok);
    return ok ? std::clamp(v, 0.0, 1.0) : 0.5;
}

void SettingsService::setVolume(double volume)
{
    m_settings.setValue(QStringLiteral("audio/volume"), std::clamp(volume, 0.0, 1.0));
    m_settings.sync();
}

bool SettingsService::muted() const
{
    return m_settings.value(QStringLiteral("audio/muted"), false).toBool();
}

void SettingsService::setMuted(bool muted)
{
    m_settings.setValue(QStringLiteral("audio/muted"), muted);
    m_settings.sync();
}

QString SettingsService::libraryPath() const
{
    return m_settings.value(QStringLiteral("library/path")).toString();
}

void SettingsService::setLibraryPath(const QString& path)
{
    m_settings.setValue(QStringLiteral("library/path"), path);
    m_settings.sync();
}

WindowGeometry SettingsService::windowGeometry() const
{
    WindowGeometry g;
    if (!m_settings.contains(QStringLiteral("window/width")))
        return g;
    g.x = m_settings.value(QStringLiteral("window/x")).toInt();
    g.y = m_settings.value(QStringLiteral("window/y")).toInt();
    g.width = m_settings.value(QStringLiteral("window/width")).toInt();
    g.height = m_settings.value(QStringLiteral("window/height")).toInt();
    g.maximised = m_settings.value(QStringLiteral("window/maximised"), false).toBool();
    g.valid = g.width > 0 && g.height > 0;
    return g;
}

void SettingsService::setWindowGeometry(const WindowGeometry& geometry)
{
    m_settings.setValue(QStringLiteral("window/x"), geometry.x);
    m_settings.setValue(QStringLiteral("window/y"), geometry.y);
    m_settings.setValue(QStringLiteral("window/width"), geometry.width);
    m_settings.setValue(QStringLiteral("window/height"), geometry.height);
    m_settings.setValue(QStringLiteral("window/maximised"), geometry.maximised);
    m_settings.sync();
}

}  // namespace semplice

namespace semplice {

QString SettingsService::uiValue(const QString& key, const QString& fallback) const
{
    return m_settings.value(QStringLiteral("ui/") + key, fallback).toString();
}

void SettingsService::setUiValue(const QString& key, const QString& value)
{
    m_settings.setValue(QStringLiteral("ui/") + key, value);
    m_settings.sync();
}

}  // namespace semplice
