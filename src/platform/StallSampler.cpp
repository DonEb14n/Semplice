#include "platform/StallSampler.h"

#include "core/util/Logger.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QtGlobal>

#include <atomic>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#endif

namespace semplice::platform {

namespace {
#ifdef Q_OS_WIN
HANDLE g_guiThread = nullptr;
std::atomic<bool> g_symbolsReady{false};
#endif

#ifdef Q_OS_WIN
QString sampleStack(HANDLE thread)
{
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
        return {};
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_FULL;
    QString out;
    if (GetThreadContext(thread, &context)) {
        STACKFRAME64 frame = {};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        HANDLE process = GetCurrentProcess();
        for (int i = 0; i < 40; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
            if (frame.AddrPC.Offset == 0)
                break;
            const DWORD64 base = SymGetModuleBase64(process, frame.AddrPC.Offset);
            char name[MAX_PATH] = {};
            if (base)
                GetModuleFileNameA(reinterpret_cast<HMODULE>(base), name, MAX_PATH);
            const QString module = QFileInfo(QString::fromLocal8Bit(name)).fileName();
            out += QStringLiteral(" %1+0x%2").arg(module.isEmpty() ? QStringLiteral("?") : module).arg(frame.AddrPC.Offset - base, 0, 16);
        }
    }
    ResumeThread(thread);
    return out;
}
#endif
}  // namespace

#ifdef Q_OS_WIN
// The stack of the Qt Quick render thread (the window waits for it while a frame is recorded).
QString renderThreadStack()
{
    using GetDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const auto getDescription = reinterpret_cast<GetDescription>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetThreadDescription"));
    if (!getDescription)
        return {};
    QString out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return {};
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snap, &entry); more; more = Thread32Next(snap, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId())
            continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!h)
            continue;
        PWSTR name = nullptr;
        if (SUCCEEDED(getDescription(h, &name)) && name) {
            if (QString::fromWCharArray(name).contains(QLatin1String("QSGRenderThread")))
                out += QStringLiteral(" || render thread:") + sampleStack(h);
            LocalFree(name);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return out;
}
#endif

QString guiStackNow()
{
#ifdef Q_OS_WIN
    if (!g_guiThread || !g_symbolsReady.exchange(true)) {
        if (!g_guiThread)
            return {};
        SymSetOptions(SYMOPT_DEFERRED_LOADS);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    }
    return sampleStack(g_guiThread) + renderThreadStack();
#else
    return {};
#endif
}

void startStallSampler(int)
{
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("SEMPLICE_STALL_TRACE") || g_guiThread)
        return;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_guiThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
#endif
}

}  // namespace semplice::platform
