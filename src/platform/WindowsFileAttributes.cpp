#include "platform/WindowsFileAttributes.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace semplice::platform {

Result<void> WindowsFileAttributes::setHidden(const QString& absolutePath)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(absolutePath);
    const auto* wide = reinterpret_cast<LPCWSTR>(native.utf16());
    const DWORD current = GetFileAttributesW(wide);
    if (current == INVALID_FILE_ATTRIBUTES)
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot read attributes of ") + absolutePath);
    if (!SetFileAttributesW(wide, current | FILE_ATTRIBUTE_HIDDEN))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot hide ") + absolutePath);
#else
    Q_UNUSED(absolutePath);
#endif
    return {};
}

}  // namespace semplice::platform
