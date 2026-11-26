#pragma once

#include "core/storage/IFileAttributes.h"

namespace semplice::platform {

// FILE_ATTRIBUTE_HIDDEN through SetFileAttributesW (no-op on other systems).
class WindowsFileAttributes final : public IFileAttributes {
public:
    Result<void> setHidden(const QString& absolutePath) override;
};

}  // namespace semplice::platform
