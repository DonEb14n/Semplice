#pragma once

#include "core/playback/PlaybackInterfaces.h"

#include <QAbstractNativeEventFilter>
#include <QThread>

#include <memory>

namespace semplice::platform {

// The Previous / Play-Pause / Next buttons under the taskbar thumbnail (SPEC 7.7), via ITaskbarList3.
// Qt 6 has no WinExtras: the buttons can only be added after Windows has created the taskbar button
// (the "TaskbarButtonCreated" message), and clicks arrive as WM_COMMAND / THBN_CLICKED, so this class
// installs a native event filter. Call attach() with the main window's HWND once the window exists.
// The calls into Explorer (ITaskbarList3) run on a thread of their own: they cross into another process and took up to a
// second on a slower PC, and the window stood still meanwhile.
class WinThumbBar final : public IThumbBar, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit WinThumbBar(QObject* parent = nullptr);
    ~WinThumbBar() override;

    void attach(quintptr hwnd);
    bool isReady() const;

    void setState(bool playing, bool canPrevious, bool canNext, bool canPlay) override;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QThread m_worker;
    QObject* m_exec = nullptr;  // lives on m_worker; work is queued to it
    bool m_haveState = false;   // the last state that was sent (an unchanged state is not sent again)
    bool m_playing = false, m_canPrev = false, m_canNext = false, m_canPlay = false;
};

}  // namespace semplice::platform
