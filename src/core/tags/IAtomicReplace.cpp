#include "core/tags/IAtomicReplace.h"

#include "core/storage/OwnFiles.h"

#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace semplice {

Result<void> PortableAtomicReplace::replaceFile(const QString& replacement, const QString& target)
{
    const QString backup = target + QStringLiteral(".semplice-old-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    if (!QFile::rename(target, backup))
        return Error::make(QFileInfo::exists(target) ? ErrorCode::FileLocked : ErrorCode::NotFound,
                           QStringLiteral("cannot set aside ") + target);
    if (!QFile::rename(replacement, target)) {
        (void)QFile::rename(backup, target);  // put the original back
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot move the new file into place: ") + target);
    }
    return ownfiles::removeFile(backup);
}

}  // namespace semplice
