#pragma once

#include "core/services/ISettings.h"

#include <QCoreApplication>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

namespace semplice {

// The Settings page (UI doc p.49-50, Figma "Middle window all conditions"): language, name, theme, library folder,
// rescan, logs, site, license. Changes are a draft until Save; a new language needs a restart (marked in the UI), a new theme
// and text size apply at once.
class SettingsVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList languages READ languages CONSTANT FINAL)       // [{ code, name }] (names in their own language)
    Q_PROPERTY(QString version READ version CONSTANT FINAL)                  // "0.10", shown in the footer
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed FINAL)
    Q_PROPERTY(QString languageName READ languageName NOTIFY changed FINAL)
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList themes READ themes CONSTANT FINAL)             // [{ code, name }]: True White, Dark, System
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed FINAL)  // the draft
    Q_PROPERTY(QString themeName READ themeName NOTIFY changed FINAL)
    Q_PROPERTY(bool darkApplied READ darkApplied NOTIFY changed FINAL)     // the saved theme (and, for System, Windows' own) is dark: what the program uses now
    Q_PROPERTY(QVariantList textScales READ textScales CONSTANT FINAL)             // [80 ... 150] (percent)
    Q_PROPERTY(int textScale READ textScale WRITE setTextScale NOTIFY changed FINAL)  // the draft
    Q_PROPERTY(int appliedTextScale READ appliedTextScale NOTIFY changed FINAL)       // the saved one: what the program uses now
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed FINAL)  // a saved language / theme differs from the running one
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed FINAL)
    Q_PROPERTY(QString libraryPath READ libraryPath NOTIFY changed FINAL)
    Q_PROPERTY(bool rescanning READ rescanning NOTIFY rescanChanged FINAL)
    Q_PROPERTY(double rescanProgress READ rescanProgress NOTIFY rescanChanged FINAL)
    Q_PROPERTY(QString licenseText READ licenseText CONSTANT FINAL)
    Q_PROPERTY(bool licenseAccepted READ licenseAccepted NOTIFY changed FINAL)
    // where the windows are (Settings -> Window placement): the order of the three columns, the queue above the player, the hotbar on top
    Q_PROPERTY(QStringList layoutOrder READ layoutOrder NOTIFY layoutChanged FINAL)
    Q_PROPERTY(bool layoutQueueAbove READ layoutQueueAbove NOTIFY layoutChanged FINAL)
    Q_PROPERTY(bool layoutHotbarTop READ layoutHotbarTop NOTIFY layoutChanged FINAL)
public:
    explicit SettingsVm(ISettings& settings, QObject* parent = nullptr);

    QVariantList languages() const;
    QString language() const { return m_language; }
    void setLanguage(const QString& code);
    QString languageName() const;
    QVariantList textScales() const { return {80, 90, 100, 115, 130, 150}; }
    int textScale() const { return m_textScale; }
    void setTextScale(int percent);
    int appliedTextScale() const;
    QString userName() const { return m_name; }
    void setUserName(const QString& name);
    QVariantList themes() const;
    QString theme() const { return m_theme; }
    void setTheme(const QString& code);
    bool darkApplied() const;
    QString themeName() const;
    bool restartNeeded() const;
    bool dirty() const;
    static QString version();   // "1.0" for 1.0.0 (a zero patch number is not shown)
    QString libraryPath() const { return m_libraryPath; }
    void setLibraryPath(const QString& path);
    bool rescanning() const { return m_rescanning; }
    double rescanProgress() const { return m_total > 0 ? double(m_done) / double(m_total) : 0.0; }
    QString licenseText() const;
    bool licenseAccepted() const;
    QStringList layoutOrder() const { return m_layoutOrder; }
    bool layoutQueueAbove() const { return m_layoutQueueAbove; }
    bool layoutHotbarTop() const { return m_layoutHotbarTop; }
    // "library", "main", "right" in the order they stand; "right" (the player and the queue) must be at an end. Kept at once.
    Q_INVOKABLE void setLayout(const QStringList& order, bool queueAbove, bool hotbarTop);
    static bool validLayoutOrder(const QStringList& order);

    // Loads the drafts from the stored settings (when the page opens).
    Q_INVOKABLE void reload();
    // Writes the drafts.
    Q_INVOKABLE void save();
    Q_INVOKABLE void openLogs() const;
    Q_INVOKABLE void openSite() const;
    Q_INVOKABLE void openSupport() const;   // the Patreon page
    Q_INVOKABLE void acceptLicense();
    // Called by the controller while a rescan runs.
    void setRescan(bool running, qint64 done = 0, qint64 total = 0);

    static QString logsFolder();
    static QUrl officialSite();

signals:
    void changed();
    void layoutChanged();
    void rescanChanged();
    void saved();                    // the name changed: greetings refresh
    void changeFolderRequested();    // QML opens the folder dialog (it owns the FolderDialog)
    void rescanRequested();
    void rescanCancelRequested();

public slots:
    void requestRescan() { emit rescanRequested(); }
    void cancelRescan() { emit rescanCancelRequested(); }

private:
    void loadLayout();
    QStringList m_layoutOrder{QStringLiteral("library"), QStringLiteral("main"), QStringLiteral("right")};
    bool m_layoutQueueAbove = false;
    bool m_layoutHotbarTop = false;
    ISettings& m_settings;
    int m_textScale = 100;
    QString m_language, m_name, m_theme;
    const QString m_runningLanguage;
    QString m_libraryPath;
    bool m_rescanning = false;
    qint64 m_done = 0, m_total = 0;
};

}  // namespace semplice
