#pragma once

#include "core/services/ISettings.h"

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QRect>

namespace semplice {

// View-model of the main window: display name and window geometry persistence.
class ShellVm : public QObject {
    Q_OBJECT
    // The name shown in the greeting; the localised "Listener" when the user set none.
    Q_PROPERTY(QString userName READ userName NOTIFY userNameChanged FINAL)

public:
    explicit ShellVm(ISettings& settings, QObject* parent = nullptr);

    QString userName() const;

    // Restores the saved geometry onto a visible screen, shows the window and keeps the
    // geometry saved when the application quits.
    void attach(QQuickWindow* window);

public slots:
    // The name was changed in the settings: greetings refresh.
    void notifyUserNameChanged() { emit userNameChanged(); }
    // Bring the window to the front (second launch of the app).
    void activate();

signals:
    void userNameChanged();

private:
    void trackNormalGeometry();
    void saveGeometry();

    ISettings& m_settings;
    QPointer<QQuickWindow> m_window;
    QRect m_normalRect;  // geometry while not maximised / minimised
};

}  // namespace semplice
