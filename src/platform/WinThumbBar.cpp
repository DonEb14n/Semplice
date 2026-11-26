#include "platform/WinThumbBar.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHints>

#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace semplice::platform {

#ifdef Q_OS_WIN
namespace {

enum ButtonId { IdPrevious = 100, IdPlayPause = 101, IdNext = 102 };

// A 32 x 32 glyph drawn with QPainter (no SVG plugin needed in the platform layer).
HICON makeIcon(const QString& glyph, bool dark)
{
    QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(dark ? QColor(0xF2, 0xF2, 0xF2) : QColor(0x20, 0x20, 0x20));
        QPainterPath path;
        if (glyph == QLatin1String("play")) {
            path.moveTo(10, 6);
            path.lineTo(26, 16);
            path.lineTo(10, 26);
            path.closeSubpath();
        } else if (glyph == QLatin1String("pause")) {
            path.addRoundedRect(8, 6, 6, 20, 1.5, 1.5);
            path.addRoundedRect(18, 6, 6, 20, 1.5, 1.5);
        } else if (glyph == QLatin1String("next")) {
            path.moveTo(6, 7);
            path.lineTo(20, 16);
            path.lineTo(6, 25);
            path.closeSubpath();
            path.addRoundedRect(22, 7, 4, 18, 1, 1);
        } else {  // previous
            path.moveTo(26, 7);
            path.lineTo(12, 16);
            path.lineTo(26, 25);
            path.closeSubpath();
            path.addRoundedRect(6, 7, 4, 18, 1, 1);
        }
        p.drawPath(path);
    }
    // a colour bitmap plus an all-zero mask: the alpha channel of the colour bitmap does the work
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = 32;
    header.bV5Height = -32;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color)
        return nullptr;
    memcpy(bits, image.constBits(), static_cast<size_t>(image.sizeInBytes()));
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace
#endif

struct WinThumbBar::Impl {
#ifdef Q_OS_WIN
    HWND hwnd = nullptr;
    ITaskbarList3* taskbar = nullptr;
    UINT buttonCreatedMessage = 0;
    std::atomic<bool> added{false};
    bool dark = false;
    HICON iconPrev = nullptr, iconPlay = nullptr, iconPause = nullptr, iconNext = nullptr;
    // last requested state, applied as soon as the buttons exist
    bool playing = false, canPrev = false, canNext = false, canPlay = false;

    void addButtons()
    {
        if (added || !hwnd)
            return;
        if (!taskbar) {
            if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar))))
                return;
            if (FAILED(taskbar->HrInit())) {
                taskbar->Release();
                taskbar = nullptr;
                return;
            }
        }
        iconPrev = makeIcon(QStringLiteral("previous"), dark);
        iconPlay = makeIcon(QStringLiteral("play"), dark);
        iconPause = makeIcon(QStringLiteral("pause"), dark);
        iconNext = makeIcon(QStringLiteral("next"), dark);
        THUMBBUTTON buttons[3] = {};
        fill(buttons[0], IdPrevious, iconPrev, QCoreApplication::translate("semplice::platform::WinThumbBar", "Previous"), canPrev);
        fill(buttons[1], IdPlayPause, playing ? iconPause : iconPlay,
             playing ? QCoreApplication::translate("semplice::platform::WinThumbBar", "Pause")
                     : QCoreApplication::translate("semplice::platform::WinThumbBar", "Play"),
             canPlay);
        fill(buttons[2], IdNext, iconNext, QCoreApplication::translate("semplice::platform::WinThumbBar", "Next"), canNext);
        added = SUCCEEDED(taskbar->ThumbBarAddButtons(hwnd, 3, buttons));
    }

    static void fill(THUMBBUTTON& b, UINT id, HICON icon, const QString& tip, bool enabled)
    {
        b.dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        b.iId = id;
        b.hIcon = icon;
        const int n = std::min<int>(static_cast<int>(tip.size()), 258);
        wcsncpy_s(b.szTip, reinterpret_cast<const wchar_t*>(tip.utf16()), static_cast<size_t>(n));
        b.dwFlags = static_cast<THUMBBUTTONFLAGS>((enabled ? THBF_ENABLED : THBF_DISABLED));
    }

    void update()
    {
        if (!added || !taskbar)
            return;
        THUMBBUTTON buttons[3] = {};
        fill(buttons[0], IdPrevious, iconPrev, QCoreApplication::translate("semplice::platform::WinThumbBar", "Previous"), canPrev);
        fill(buttons[1], IdPlayPause, playing ? iconPause : iconPlay,
             playing ? QCoreApplication::translate("semplice::platform::WinThumbBar", "Pause")
                     : QCoreApplication::translate("semplice::platform::WinThumbBar", "Play"),
             canPlay);
        fill(buttons[2], IdNext, iconNext, QCoreApplication::translate("semplice::platform::WinThumbBar", "Next"), canNext);
        taskbar->ThumbBarUpdateButtons(hwnd, 3, buttons);
    }
#endif
};

WinThumbBar::WinThumbBar(QObject* parent) : IThumbBar(parent), m_impl(std::make_unique<Impl>())
{
    m_exec = new QObject;
    m_exec->moveToThread(&m_worker);
#ifdef Q_OS_WIN
    connect(&m_worker, &QThread::started, m_exec, [] { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }, Qt::DirectConnection);
#endif
    m_worker.setObjectName(QStringLiteral("semplice-taskbar"));
    m_worker.start();
}

WinThumbBar::~WinThumbBar()
{
#ifdef Q_OS_WIN
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
    auto& s = *m_impl;
    // what is queued is finished, then the COM object is let go of on the thread that made it
    QMetaObject::invokeMethod(m_exec, [&s] {
        if (s.taskbar)
            s.taskbar->Release();
        s.taskbar = nullptr;
        for (HICON icon : {s.iconPrev, s.iconPlay, s.iconPause, s.iconNext})
            if (icon)
                DestroyIcon(icon);
        CoUninitialize();
    }, Qt::BlockingQueuedConnection);
#endif
    m_worker.quit();
    m_worker.wait();
    delete m_exec;
}

bool WinThumbBar::isReady() const
{
#ifdef Q_OS_WIN
    return m_impl->added;
#else
    return false;
#endif
}

void WinThumbBar::attach(quintptr hwnd)
{
#ifdef Q_OS_WIN
    auto& s = *m_impl;
    if (s.hwnd)
        return;
    s.hwnd = reinterpret_cast<HWND>(hwnd);
    s.buttonCreatedMessage = RegisterWindowMessageW(L"TaskbarButtonCreated");
    s.dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    QCoreApplication::instance()->installNativeEventFilter(this);
    QMetaObject::invokeMethod(m_exec, [&s] { s.addButtons(); }, Qt::QueuedConnection);  // works if the taskbar button exists already; otherwise the message below does it
#else
    Q_UNUSED(hwnd)
#endif
}

void WinThumbBar::setState(bool playing, bool canPrevious, bool canNext, bool canPlay)
{
#ifdef Q_OS_WIN
    if (m_haveState && playing == m_playing && canPrevious == m_canPrev && canNext == m_canNext && canPlay == m_canPlay)
        return;  // the same buttons again (a save announces the same track): nothing to send to Explorer
    m_haveState = true;
    m_playing = playing;
    m_canPrev = canPrevious;
    m_canNext = canNext;
    m_canPlay = canPlay;
    auto& s = *m_impl;
    QMetaObject::invokeMethod(m_exec, [&s, playing, canPrevious, canNext, canPlay] {
        s.playing = playing;
        s.canPrev = canPrevious;
        s.canNext = canNext;
        s.canPlay = canPlay;
        s.update();
    }, Qt::QueuedConnection);
#else
    Q_UNUSED(playing)
    Q_UNUSED(canPrevious)
    Q_UNUSED(canNext)
    Q_UNUSED(canPlay)
#endif
}

bool WinThumbBar::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" || !message)
        return false;
    const MSG* msg = static_cast<const MSG*>(message);
    auto& s = *m_impl;
    if (msg->hwnd != s.hwnd)
        return false;
    if (s.buttonCreatedMessage != 0 && msg->message == s.buttonCreatedMessage) {
        s.dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        QMetaObject::invokeMethod(m_exec, [&s] {
            s.added = false;  // Explorer restarted or the button was recreated
            s.addButtons();
            s.update();
        }, Qt::QueuedConnection);
        return false;
    }
    if (msg->message == WM_COMMAND && HIWORD(msg->wParam) == THBN_CLICKED) {
        switch (LOWORD(msg->wParam)) {
        case IdPrevious: emit previousClicked(); return true;
        case IdPlayPause: emit playPauseClicked(); return true;
        case IdNext: emit nextClicked(); return true;
        default: break;
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}

}  // namespace semplice::platform
