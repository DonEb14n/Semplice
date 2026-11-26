#include "core/storage/ITrash.h"

#include <QFile>

namespace semplice {

Result<void> QtTrash::moveToTrash(const QString& absolutePath)
{
    if (!QFile::moveToTrash(absolutePath))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot move to the Recycle Bin: ") + absolutePath);
    return {};
}

}  // namespace semplice
