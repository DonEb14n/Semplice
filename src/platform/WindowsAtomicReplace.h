#pragma once

#include "core/tags/IAtomicReplace.h"

namespace semplice::platform {

// ReplaceFileW: swaps the replacement in for the target in one step (no moment without a file).
// On other systems it falls back to the portable implementation.
class WindowsAtomicReplace final : public IAtomicReplace {
public:
    Result<void> replaceFile(const QString& replacement, const QString& target) override;
};

}  // namespace semplice::platform
