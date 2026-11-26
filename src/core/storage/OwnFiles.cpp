#include "core/storage/OwnFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace semplice::ownfiles {

Result<void> removeFile(const QString& absolutePath)
{
    if (absolutePath.isEmpty() || !QFileInfo(absolutePath).isAbsolute())
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("removeFile needs an absolute path"));
    const QFileInfo info(absolutePath);
    if (!info.exists() && !info.isSymLink())
        return {};
    if (info.isDir())
        return Error::make(ErrorCode::NotAllowed, QStringLiteral("removeFile refuses folders: ") + absolutePath);
    if (!QFile::remove(absolutePath))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot remove ") + absolutePath);
    return {};
}

Result<void> removeEmptyDir(const QString& absolutePath)
{
    if (absolutePath.isEmpty() || !QFileInfo(absolutePath).isAbsolute())
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("removeEmptyDir needs an absolute path"));
    const QFileInfo info(absolutePath);
    if (!info.exists())
        return {};
    if (!info.isDir())
        return Error::make(ErrorCode::NotAllowed, QStringLiteral("not a folder: ") + absolutePath);
    if (!QDir(absolutePath).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        return Error::make(ErrorCode::NotAllowed, QStringLiteral("folder not empty: ") + absolutePath);
    if (!QDir().rmdir(absolutePath))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot remove folder ") + absolutePath);
    return {};
}

}  // namespace semplice::ownfiles
