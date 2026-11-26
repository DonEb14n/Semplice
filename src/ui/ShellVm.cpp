#include "ui/ShellVm.h"

#include "core/util/WindowGeometryPolicy.h"

#include <QGuiApplication>
#include <QScreen>

namespace semplice {

ShellVm::ShellVm(ISettings& settings, QObject* parent) : QObject(parent), m_settings(settings) {}

QString ShellVm::userName() const
{
    const QString name = m_settings.userName();
    return name.isEmpty() ? tr("Listener") : name;
}

void ShellVm::attach(QQuickWindow* window)
{
    m_window = window;

    QList<QRect> screens;
    for (const QScreen* screen : QGuiApplication::screens())
        screens.append(screen->availableGeometry());
    // The primary screen must come first: it is the fallback position.
    if (QScreen* primary = QGuiApplication::primaryScreen())
        screens.move(screens.indexOf(primary->availableGeometry()), 0);

    const WindowGeometry saved = m_settings.windowGeometry();
    const QSize minSize(window->minimumWidth(), window->minimumHeight());
    m_normalRect = resolveWindowRect(saved, screens, minSize, QSize(window->width(), window->height()));
    window->setGeometry(m_normalRect);

    trackNormalGeometry();
    connect(qApp, &QGuiApplication::aboutToQuit, this, &ShellVm::saveGeometry);

    if (saved.valid && saved.maximised)
        window->showMaximized();
    else
        window->show();
}

void ShellVm::trackNormalGeometry()
{
    auto update = [this] {
        if (m_window && m_window->visibility() == QWindow::Windowed)
            m_normalRect = m_window->geometry();
    };
    connect(m_window, &QWindow::xChanged, this, update);
    connect(m_window, &QWindow::yChanged, this, update);
    connect(m_window, &QWindow::widthChanged, this, update);
    connect(m_window, &QWindow::heightChanged, this, update);
}

void ShellVm::saveGeometry()
{
    if (!m_window)
        return;
    WindowGeometry g;
    g.x = m_normalRect.x();
    g.y = m_normalRect.y();
    g.width = m_normalRect.width();
    g.height = m_normalRect.height();
    g.maximised = m_window->visibility() == QWindow::Maximized;
    g.valid = g.width > 0 && g.height > 0;
    m_settings.setWindowGeometry(g);
}

void ShellVm::activate()
{
    if (!m_window)
        return;
    if (m_window->visibility() == QWindow::Minimized)
        m_window->showNormal();
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

}  // namespace semplice
