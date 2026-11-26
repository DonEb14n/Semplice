#include "platform/WinSmtc.h"

#include "core/util/Logger.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <shcore.h>
#include <shlwapi.h>
#include <winstring.h>

#include <systemmediatransportcontrolsinterop.h>
#endif

namespace semplice::platform {

#ifdef Q_OS_WIN
// NOT an anonymous namespace: these interfaces are implemented by Windows, and for a type with internal linkage and no
// implementation in this file GCC concludes that no object can exist and turns every virtual call into a trap.
namespace winrt {

// ---- the WinRT interfaces we use, declared by hand ---------------------------------------------------
// Method ORDER is the ABI: copied from the Windows SDK 10.0.26100 winrt headers (windows.media.h,
// windows.storage.streams.h, windows.foundation.h). Unused slots are placeholders.
const GUID kIidSmtcInterop = {0xddb0472d, 0xc911, 0x4a1f, {0x86, 0xd9, 0xdc, 0x3d, 0x71, 0xa9, 0x5f, 0x5a}};
const GUID kIidSmtc = {0x99fa3ff4, 0x1742, 0x42a6, {0x90, 0x2e, 0x08, 0x7d, 0x41, 0xf9, 0x65, 0xec}};
const GUID kIidMusicProps2 = {0x00368462, 0x97d3, 0x44b9, {0xb0, 0x0f, 0x00, 0x8a, 0xfc, 0xef, 0xaf, 0x18}};
// ITypedEventHandler<SystemMediaTransportControls*, SystemMediaTransportControlsButtonPressedEventArgs*>
const GUID kIidButtonHandler = {0x0557e996, 0x7b23, 0x5bae, {0xaa, 0x81, 0xea, 0x0d, 0x67, 0x11, 0x43, 0xa4}};
const GUID kIidStreamRefStatics = {0x857309dc, 0x3fbf, 0x4e7d, {0x98, 0x6f, 0xef, 0x3b, 0x1a, 0x07, 0xa9, 0x64}};
const GUID kIidUriFactory = {0x44a9796f, 0x723e, 0x4fdf, {0xa2, 0x18, 0x03, 0x3e, 0x75, 0xb0, 0xc0, 0x84}};
const GUID kIidRandomAccessStream = {0x905a0fe1, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};
const GUID kIidAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

struct RegToken {  // Windows.Foundation.EventRegistrationToken
    __int64 value;
};

enum Button { ButtonPlay = 0, ButtonPause = 1, ButtonNext = 6, ButtonPrevious = 7 };
enum PlaybackStatus { StatusStopped = 2, StatusPlaying = 3, StatusPaused = 4 };
constexpr int kTypeMusic = 1;

struct IStreamRef : IInspectable {};  // IRandomAccessStreamReference: only passed around

struct IMusicProps : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumArtist(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Artist(HSTRING) = 0;
};
struct IMusicProps2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumTitle(HSTRING) = 0;
};
struct IDisplayUpdater : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Type(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Type(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AppMediaId(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AppMediaId(HSTRING) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(IStreamRef**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Thumbnail(IStreamRef*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MusicProperties(IMusicProps**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_VideoProperties(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ImageProperties(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromFileAsync(int, void*, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAll() = 0;
    virtual HRESULT STDMETHODCALLTYPE Update() = 0;
};
struct IButtonArgs : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Button(int*) = 0;
};
struct IButtonHandler : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable* sender, IButtonArgs* args) = 0;
};
struct ISmtc : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackStatus(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayUpdater(IDisplayUpdater**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SoundLevel(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPlayEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStopEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPauseEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRecordEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsFastForwardEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRewindEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPreviousEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsNextEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelUpEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelUpEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelDownEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelDownEnabled(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ButtonPressed(IButtonHandler*, RegToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ButtonPressed(RegToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PropertyChanged(void*, RegToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PropertyChanged(RegToken) = 0;
};
struct IStreamRefStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFromFile(void*, IStreamRef**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromUri(IInspectable* uri, IStreamRef**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromStream(void*, IStreamRef**) = 0;
};
struct IUriFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateUri(HSTRING, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWithRelativeUri(HSTRING, HSTRING, IInspectable**) = 0;
};

// Owns an HSTRING.
struct HString {
    explicit HString(const QString& text)
    {
        WindowsCreateString(reinterpret_cast<const wchar_t*>(text.utf16()), static_cast<UINT32>(text.size()), &h);
    }
    ~HString() { WindowsDeleteString(h); }
    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;
    HSTRING h = nullptr;
};

template <class T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    void reset()
    {
        if (p)
            p->Release();
        p = nullptr;
    }
    T** put()
    {
        reset();
        return &p;
    }
    // For COM calls that return the interface as void**: the pointer goes through a void* (no type punning).
    template <class F>
    HRESULT acquire(F&& call)
    {
        reset();
        void* raw = nullptr;
        const HRESULT hr = call(&raw);
        if (SUCCEEDED(hr))
            p = static_cast<T*>(raw);
        return hr;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// The "button pressed" delegate. WinRT calls it on a worker thread, so it only forwards to the Qt thread.
class ButtonHandler final : public IButtonHandler {
public:
    explicit ButtonHandler(QObject* target) : m_target(target) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, kIidButtonHandler) || IsEqualIID(iid, kIidAgileObject)) {
            *out = static_cast<IButtonHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&m_refs)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG n = InterlockedDecrement(&m_refs);
        if (n == 0)
            delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, IButtonArgs* args) override
    {
        int button = -1;
        QObject* target = m_target.loadAcquire();
        if (target && args && SUCCEEDED(args->get_Button(&button)))
            QMetaObject::invokeMethod(target, "onButton", Qt::QueuedConnection, Q_ARG(int, button));
        return S_OK;
    }
    void detach() { m_target.storeRelease(nullptr); }

private:
    QAtomicPointer<QObject> m_target;
    LONG m_refs = 1;
};

}  // namespace winrt
using namespace winrt;
#endif  // Q_OS_WIN

struct WinSmtc::Impl {
#ifdef Q_OS_WIN
    ComPtr<ISmtc> smtc;
    ComPtr<IDisplayUpdater> updater;
    ComPtr<IMusicProps> music;
    ComPtr<IMusicProps2> music2;
    ComPtr<IStreamRefStatics> streamRefs;
    ComPtr<IUriFactory> uriFactory;
    ButtonHandler* handler = nullptr;
    RegToken token{};
    bool registered = false;
#endif
};

WinSmtc::WinSmtc(QObject* parent) : ISystemMediaControls(parent), m_impl(std::make_unique<Impl>())
{
    m_exec = new QObject;
    m_exec->moveToThread(&m_worker);
    connect(&m_worker, &QThread::started, m_exec, [] {
#ifdef Q_OS_WIN
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    }, Qt::DirectConnection);
    m_worker.setObjectName(QStringLiteral("semplice-media-controls"));
    m_worker.start();
}

void WinSmtc::setMetadata(const MediaMetadata& metadata)
{
    QMetaObject::invokeMethod(m_exec, [this, metadata] { applyMetadata(metadata); }, Qt::QueuedConnection);
}

void WinSmtc::setState(PlayState state)
{
    QMetaObject::invokeMethod(m_exec, [this, state] { applyState(state); }, Qt::QueuedConnection);
}

void WinSmtc::setEnabled(bool canPrevious, bool canNext, bool canPlay)
{
    QMetaObject::invokeMethod(m_exec, [this, canPrevious, canNext, canPlay] { applyEnabled(canPrevious, canNext, canPlay); }, Qt::QueuedConnection);
}

WinSmtc::~WinSmtc()
{
    // everything queued so far is done first, then the thread ends
    QMetaObject::invokeMethod(m_exec, [] {}, Qt::BlockingQueuedConnection);
    m_worker.quit();
    m_worker.wait();
    delete m_exec;
#ifdef Q_OS_WIN
    if (m_impl->handler)
        m_impl->handler->detach();  // a callback that is already running must not touch this object any more
    if (m_impl->smtc) {
        if (m_impl->registered)
            m_impl->smtc->remove_ButtonPressed(m_impl->token);
        m_impl->smtc->put_IsEnabled(false);
    }
    if (m_impl->handler)
        m_impl->handler->Release();
#endif
}

bool WinSmtc::isAttached() const
{
#ifdef Q_OS_WIN
    return static_cast<bool>(m_impl->smtc);
#else
    return false;
#endif
}

bool WinSmtc::attach(quintptr hwnd)
{
#ifdef Q_OS_WIN
    auto& s = *m_impl;
    if (s.smtc)
        return true;
    // Qt has initialised COM on the GUI thread; WinRT needs nothing more.
    ComPtr<ISystemMediaTransportControlsInterop> interop;
    {
        HString name(QStringLiteral("Windows.Media.SystemMediaTransportControls"));
        if (FAILED(interop.acquire([&](void** out) { return RoGetActivationFactory(name.h, kIidSmtcInterop, out); })))
            return false;
    }
    if (FAILED(s.smtc.acquire([&](void** out) { return interop->GetForWindow(reinterpret_cast<HWND>(hwnd), kIidSmtc, out); })))
        return false;
    s.smtc->put_IsEnabled(true);
    s.smtc->put_IsPlayEnabled(true);
    s.smtc->put_IsPauseEnabled(true);
    s.smtc->put_IsPreviousEnabled(false);
    s.smtc->put_IsNextEnabled(false);
    if (SUCCEEDED(s.smtc->get_DisplayUpdater(s.updater.put()))) {
        s.updater->put_Type(kTypeMusic);
        s.updater->get_MusicProperties(s.music.put());
        if (s.music)
            s.music2.acquire([&](void** out) { return s.music->QueryInterface(kIidMusicProps2, out); });
    }
    s.handler = new ButtonHandler(this);
    s.registered = SUCCEEDED(s.smtc->add_ButtonPressed(s.handler, &s.token));
    {
        HString streams(QStringLiteral("Windows.Storage.Streams.RandomAccessStreamReference"));
        s.streamRefs.acquire([&](void** out) { return RoGetActivationFactory(streams.h, kIidStreamRefStatics, out); });
        HString uri(QStringLiteral("Windows.Foundation.Uri"));
        s.uriFactory.acquire([&](void** out) { return RoGetActivationFactory(uri.h, kIidUriFactory, out); });
    }
    return s.registered;
#else
    Q_UNUSED(hwnd)
    return false;
#endif
}

void WinSmtc::applyMetadata(const MediaMetadata& metadata)
{
#ifdef Q_OS_WIN
    QElapsedTimer timing;
    timing.start();
    struct Done {
        QElapsedTimer& t;
        ~Done()
        {
            if (t.elapsed() > 30)
                SLOG_WARN(QStringLiteral("SMTC setMetadata took %1 ms (on its own thread)").arg(t.elapsed()));
        }
    } done{timing};
    auto& s = *m_impl;
    if (!s.updater)
        return;
    s.updater->put_Type(kTypeMusic);
    if (s.music) {
        HString title(metadata.title);
        HString artist(metadata.artist);
        s.music->put_Title(title.h);
        s.music->put_Artist(artist.h);
    }
    if (s.music2) {
        HString album(metadata.album);
        s.music2->put_AlbumTitle(album.h);
    }
    // the cover picture file, if the track has one next to the music
    ComPtr<IStreamRef> thumbnail;
    QByteArray picture = metadata.coverData;
    if (picture.isEmpty() && !metadata.coverPath.isEmpty()) {
        QFile file(metadata.coverPath);
        if (file.open(QIODevice::ReadOnly) && file.size() < 20 * 1024 * 1024)
            picture = file.readAll();
    }
    if (!picture.isEmpty() && s.streamRefs) {
        // the bytes go to Windows as an in-memory stream (a file:// address is not readable for every shell host)
        IStream* memory = SHCreateMemStream(reinterpret_cast<const BYTE*>(picture.constData()), static_cast<UINT>(picture.size()));
        if (memory) {
            void* randomAccess = nullptr;
            if (SUCCEEDED(CreateRandomAccessStreamOverStream(memory, BSOS_DEFAULT, kIidRandomAccessStream, &randomAccess)) && randomAccess) {
                s.streamRefs->CreateFromStream(randomAccess, thumbnail.put());
                static_cast<IUnknown*>(randomAccess)->Release();
            }
            memory->Release();
        }
    }
    if (qEnvironmentVariableIsSet("SEMPLICE_NO_SMTC_THUMB"))
        thumbnail.reset();
    s.updater->put_Thumbnail(thumbnail.p);  // null clears the previous picture
    s.updater->Update();
#else
    Q_UNUSED(metadata)
#endif
}

void WinSmtc::applyState(PlayState state)
{
#ifdef Q_OS_WIN
    if (!m_impl->smtc)
        return;
    m_impl->smtc->put_PlaybackStatus(state == PlayState::Playing  ? StatusPlaying
                                     : state == PlayState::Paused ? StatusPaused
                                                                  : StatusStopped);
#else
    Q_UNUSED(state)
#endif
}

void WinSmtc::applyEnabled(bool canPrevious, bool canNext, bool canPlay)
{
#ifdef Q_OS_WIN
    if (!m_impl->smtc)
        return;
    m_impl->smtc->put_IsPreviousEnabled(canPrevious);
    m_impl->smtc->put_IsNextEnabled(canNext);
    m_impl->smtc->put_IsPlayEnabled(canPlay);
    m_impl->smtc->put_IsPauseEnabled(canPlay);
#else
    Q_UNUSED(canPrevious)
    Q_UNUSED(canNext)
    Q_UNUSED(canPlay)
#endif
}

void WinSmtc::onButton(int button)
{
#ifdef Q_OS_WIN
    switch (button) {
    case ButtonPlay: emit playRequested(); break;
    case ButtonPause: emit pauseRequested(); break;
    case ButtonNext: emit nextRequested(); break;
    case ButtonPrevious: emit previousRequested(); break;
    default: break;
    }
#else
    Q_UNUSED(button)
#endif
}

}  // namespace semplice::platform
