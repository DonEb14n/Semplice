#include "platform/FramelessAnimation.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace semplice::platform {

void FramelessAnimation::install(quintptr hwnd)
{
#ifdef Q_OS_WIN
    if (m_hwnd || !hwnd)
        return;
    m_hwnd = hwnd;
    const HWND h = reinterpret_cast<HWND>(hwnd);
    const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    SetWindowLongPtrW(h, GWL_STYLE, style | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    // the new styles take effect (and the client area is calculated again) without moving or resizing the window
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    QCoreApplication::instance()->installNativeEventFilter(this);
#else
    Q_UNUSED(hwnd)
#endif
}

bool FramelessAnimation::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" || !message)
        return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->hwnd != reinterpret_cast<HWND>(m_hwnd) || msg->message != WM_NCCALCSIZE || msg->wParam == FALSE)
        return false;
    // no frame, no caption: the client area is the whole window ...
    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
    if (IsZoomed(msg->hwnd)) {
        // ... except that a maximised window with a sizing frame is laid out that many pixels beyond the screen
        const int frame = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        params->rgrc[0].left += frame;
        params->rgrc[0].right -= frame;
        params->rgrc[0].top += frame;
        params->rgrc[0].bottom -= frame;
    }
    *result = 0;
    return true;
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

}  // namespace semplice::platform
