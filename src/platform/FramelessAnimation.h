#pragma once

#include <QAbstractNativeEventFilter>
#include <QtGlobal>

namespace semplice::platform {

// A window without Windows' own title bar (Qt::FramelessWindowHint) has no minimise / restore animation, because Windows plays
// those for windows that have a caption and a sizing frame. This gives the window those styles again and keeps the frame
// invisible: the whole window is client area (WM_NCCALCSIZE), a maximised window is cut back to the screen's work area.
// Windows only; elsewhere install() does nothing.
class FramelessAnimation final : public QAbstractNativeEventFilter {
public:
    void install(quintptr hwnd);
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    quintptr m_hwnd = 0;
};

}  // namespace semplice::platform
