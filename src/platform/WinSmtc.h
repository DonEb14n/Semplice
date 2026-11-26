#pragma once

#include "core/playback/PlaybackInterfaces.h"

#include <QThread>

#include <memory>

namespace semplice::platform {

// System Media Transport Controls (SPEC 7.7): the media widget in the Windows volume flyout, the lock
// screen, and the GLOBAL media keys. Title, artist, album and the cover thumbnail are published; Play,
// Pause, Next and Previous come back as signals.
//
// The WinRT interfaces are declared by hand in the .cpp (the MinGW headers lack them); the vtable layouts
// were copied from the Windows SDK 10.0.26100 headers. Needs the HWND of the main window: call attach()
// once the window exists. Without attach() (or on other systems) every call is a harmless no-op.
class WinSmtc final : public ISystemMediaControls {
    Q_OBJECT
public:
    explicit WinSmtc(QObject* parent = nullptr);
    ~WinSmtc() override;

    // `hwnd` is the native window handle (QWindow::winId()). Returns false if the controls are unavailable.
    bool attach(quintptr hwnd);
    bool isAttached() const;

    void setMetadata(const MediaMetadata& metadata) override;
    void setState(PlayState state) override;
    void setEnabled(bool canPrevious, bool canNext, bool canPlay) override;

    // The controls are updated on a thread of their own: a call into the system service can take a second and the window must
    // not wait for it (the objects are agile, any thread may use them).
    void applyMetadata(const MediaMetadata& metadata);
    void applyState(PlayState state);
    void applyEnabled(bool canPrevious, bool canNext, bool canPlay);

private slots:
    void onButton(int button);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QThread m_worker;
    QObject* m_exec = nullptr;
};

}  // namespace semplice::platform
