#pragma once

#include "core/util/Result.h"

namespace semplice {

// Sends a file or folder to the Windows Recycle Bin (SPEC 4.4). The library never deletes permanently.
class ITrash {
public:
    virtual ~ITrash() = default;
    virtual Result<void> moveToTrash(const QString& absolutePath) = 0;
};

// Real implementation: QFile::moveToTrash (IFileOperation with undo on Windows, the freedesktop trash elsewhere).
class QtTrash final : public ITrash {
public:
    Result<void> moveToTrash(const QString& absolutePath) override;
};

}  // namespace semplice
