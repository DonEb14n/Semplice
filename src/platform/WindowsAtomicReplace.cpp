#include "platform/WindowsAtomicReplace.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace semplice::platform {

Result<void> WindowsAtomicReplace::replaceFile(const QString& replacement, const QString& target)
{
#ifdef Q_OS_WIN
    const QString from = QDir::toNativeSeparators(replacement);
    const QString to = QDir::toNativeSeparators(target);
    if (ReplaceFileW(reinterpret_cast<LPCWSTR>(to.utf16()), reinterpret_cast<LPCWSTR>(from.utf16()), nullptr,
                     REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
        return {};
    const DWORD err = GetLastError();
    const bool locked = err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_ACCESS_DENIED;
    return Error::make(locked ? ErrorCode::FileLocked : ErrorCode::IoError,
                       QStringLiteral("ReplaceFileW failed (%1) for %2").arg(err).arg(target));
#else
    PortableAtomicReplace portable;
    return portable.replaceFile(replacement, target);
#endif
}

}  // namespace semplice::platform
