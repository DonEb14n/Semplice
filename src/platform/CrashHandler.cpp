#include "platform/CrashHandler.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace semplice::platform {

namespace {

wchar_t g_dir[MAX_PATH * 2] = {};
wchar_t g_version[64] = {};

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info)
{
    // Keep this minimal: the process state is unknown, so only plain Win32 and the C runtime.
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH * 2 + 64];
    swprintf_s(path, L"%s\\crash-%04d%02d%02d-%02d%02d%02d.txt", g_dir, t.wYear, t.wMonth, t.wDay, t.wHour,
               t.wMinute, t.wSecond);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        char text[512];
        const int n = sprintf_s(text,
                                "Semplice %ls\r\nException code: 0x%08lX\r\nAddress: 0x%p\r\nThread id: %lu\r\n",
                                g_version, static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
                                info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
        DWORD written = 0;
        if (n > 0)
            WriteFile(file, text, static_cast<DWORD>(n), &written, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;  // let Windows terminate the process as usual
}

}  // namespace

void installCrashHandler(const QString& logDir, const QString& appVersion)
{
    QDir().mkpath(logDir);
    const QString native = QDir::toNativeSeparators(logDir);
    wcsncpy_s(g_dir, reinterpret_cast<const wchar_t*>(native.utf16()), _TRUNCATE);
    wcsncpy_s(g_version, reinterpret_cast<const wchar_t*>(appVersion.utf16()), _TRUNCATE);
    SetUnhandledExceptionFilter(crashFilter);
}

}  // namespace semplice::platform

#else

namespace semplice::platform {
void installCrashHandler(const QString&, const QString&) {}
}  // namespace semplice::platform

#endif
