#include "ui/SettingsVm.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QStyleHints>
#include <QFile>
#include <QStandardPaths>

namespace semplice {

SettingsVm::SettingsVm(ISettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings), m_runningLanguage(settings.language())
{
    reload();
    loadLayout();
    // "System": follow Windows when its colour scheme changes
    if (QGuiApplication::instance())
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &SettingsVm::changed);
}

QVariantList SettingsVm::languages() const
{
    // each language is named in itself, so a user who cannot read the current one can still find theirs
    auto entry = [](const char* code, const QString& name) { return QVariantMap{{QStringLiteral("code"), QString::fromLatin1(code)}, {QStringLiteral("name"), name}}; };
    return {entry("en", QStringLiteral("English")), entry("ru", QStringLiteral("Русский")), entry("zh_CN", QStringLiteral("简体中文 (Beta)")),
            entry("ja", QStringLiteral("日本語 (Beta)"))};
}

QString SettingsVm::languageName() const
{
    for (const QVariant& v : languages())
        if (v.toMap().value(QStringLiteral("code")).toString() == m_language)
            return v.toMap().value(QStringLiteral("name")).toString();
    return m_language;
}

QVariantList SettingsVm::themes() const
{
    auto entry = [](const char* code, const QString& name) { return QVariantMap{{QStringLiteral("code"), QString::fromLatin1(code)}, {QStringLiteral("name"), name}}; };
    return {entry("true-white", QCoreApplication::translate("semplice::SettingsVm", "True White")),
            entry("dark", QCoreApplication::translate("semplice::SettingsVm", "Dark")),
            entry("system", QCoreApplication::translate("semplice::SettingsVm", "System"))};
}

QString SettingsVm::themeName() const
{
    for (const QVariant& v : themes())
        if (v.toMap().value(QStringLiteral("code")).toString() == m_theme)
            return v.toMap().value(QStringLiteral("name")).toString();
    return QCoreApplication::translate("semplice::SettingsVm", "True White");
}

void SettingsVm::setTheme(const QString& code)
{
    if (code == m_theme)
        return;
    m_theme = code;
    emit changed();
}

bool SettingsVm::darkApplied() const
{
    const QString saved = m_settings.theme();
    if (saved == QLatin1String("dark"))
        return true;
    if (saved == QLatin1String("system") && QGuiApplication::instance())
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    return false;
}

void SettingsVm::setLanguage(const QString& code)
{
    if (code == m_language)
        return;
    m_language = code;
    emit changed();
}

void SettingsVm::setTextScale(int percent)
{
    if (percent == m_textScale)
        return;
    m_textScale = percent;
    emit changed();
}

int SettingsVm::appliedTextScale() const
{
    const int v = m_settings.uiValue(QStringLiteral("textScale"), QStringLiteral("100")).toInt();
    return v >= 70 && v <= 200 ? v : 100;  // text size (percent): applies at once, no restart
}

void SettingsVm::setUserName(const QString& name)
{
    if (name == m_name)
        return;
    m_name = name;
    emit changed();
}

void SettingsVm::setLibraryPath(const QString& path)
{
    if (path == m_libraryPath)
        return;
    m_libraryPath = path;
    emit changed();
}

bool SettingsVm::restartNeeded() const
{
    return m_settings.language() != m_runningLanguage;
}

bool SettingsVm::dirty() const
{
    return m_language != m_settings.language() || m_name != m_settings.userName() || m_theme != m_settings.theme() || m_textScale != appliedTextScale();
}

void SettingsVm::reload()
{
    m_language = m_settings.language();
    m_name = m_settings.userName();
    m_theme = m_settings.theme();
    m_textScale = appliedTextScale();
    m_libraryPath = m_settings.libraryPath();
    emit changed();
}

void SettingsVm::save()
{
    const bool nameChanged = m_name != m_settings.userName();
    m_settings.setLanguage(m_language);
    m_settings.setUserName(m_name.trimmed());
    m_settings.setTheme(m_theme);
    m_settings.setUiValue(QStringLiteral("textScale"), QString::number(m_textScale));
    emit changed();
    if (nameChanged)
        emit saved();
}

QString SettingsVm::logsFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("Semplice/logs"));
}

QString SettingsVm::version()
{
    QString v = QCoreApplication::applicationVersion();
    if (v.count(QLatin1Char('.')) == 2 && v.endsWith(QStringLiteral(".0")))
        v.chop(2);
    return v;
}

QUrl SettingsVm::officialSite()
{
    return QUrl(QStringLiteral("https://github.com/DonEb14n/Semplice"));
}

void SettingsVm::openLogs() const
{
    QDir().mkpath(logsFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(logsFolder()));
}

void SettingsVm::openSite() const
{
    QDesktopServices::openUrl(officialSite());
}

void SettingsVm::openSupport() const
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.patreon.com/DonEb14n")));
}

QString SettingsVm::licenseText() const
{
    QFile f(QStringLiteral(":/license/LICENSE.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

bool SettingsVm::validLayoutOrder(const QStringList& order)
{
    return order.size() == 3 && order.contains(QStringLiteral("library")) && order.contains(QStringLiteral("main")) &&
           order.contains(QStringLiteral("right")) && order.at(1) != QLatin1String("right");
}

void SettingsVm::loadLayout()
{
    const QStringList order = m_settings.uiValue(QStringLiteral("layout/order"), QString()).split(QLatin1Char('|'), Qt::SkipEmptyParts);
    if (validLayoutOrder(order))
        m_layoutOrder = order;
    m_layoutQueueAbove = m_settings.uiValue(QStringLiteral("layout/queueAbove"), QStringLiteral("0")) == QLatin1String("1");
    m_layoutHotbarTop = m_settings.uiValue(QStringLiteral("layout/hotbarTop"), QStringLiteral("0")) == QLatin1String("1");
}

void SettingsVm::setLayout(const QStringList& order, bool queueAbove, bool hotbarTop)
{
    if (!validLayoutOrder(order))
        return;
    m_layoutOrder = order;
    m_layoutQueueAbove = queueAbove;
    m_layoutHotbarTop = hotbarTop;
    m_settings.setUiValue(QStringLiteral("layout/order"), order.join(QLatin1Char('|')));
    m_settings.setUiValue(QStringLiteral("layout/queueAbove"), queueAbove ? QStringLiteral("1") : QStringLiteral("0"));
    m_settings.setUiValue(QStringLiteral("layout/hotbarTop"), hotbarTop ? QStringLiteral("1") : QStringLiteral("0"));
    emit layoutChanged();
}

bool SettingsVm::licenseAccepted() const
{
    return m_settings.uiValue(QStringLiteral("license/accepted"), QString()) == QLatin1String("1");
}

void SettingsVm::acceptLicense()
{
    m_settings.setUiValue(QStringLiteral("license/accepted"), QStringLiteral("1"));
    emit changed();
}

void SettingsVm::setRescan(bool running, qint64 done, qint64 total)
{
    m_rescanning = running;
    m_done = done;
    m_total = total;
    emit rescanChanged();
}

}  // namespace semplice
