#pragma once

#include "core/util/Result.h"

namespace semplice {

// Windows file attributes behind an interface (SPEC 2: Windows code lives in platform/).
class IFileAttributes {
public:
    virtual ~IFileAttributes() = default;
    // FILE_ATTRIBUTE_HIDDEN on Windows.
    virtual Result<void> setHidden(const QString& absolutePath) = 0;
};

// Does nothing (other platforms, tests that do not care).
class NullFileAttributes final : public IFileAttributes {
public:
    Result<void> setHidden(const QString&) override { return {}; }
};

}  // namespace semplice
