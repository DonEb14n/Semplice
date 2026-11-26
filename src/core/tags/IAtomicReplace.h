#pragma once

#include "core/util/Result.h"

namespace semplice {

// "Replace `target` by `replacement`" so that a crash leaves either the old or the new file, never
// a half-written one. The replacement file is consumed (it no longer exists afterwards).
class IAtomicReplace {
public:
    virtual ~IAtomicReplace() = default;
    // E_FILE_LOCKED if another process holds `target` open.
    virtual Result<void> replaceFile(const QString& replacement, const QString& target) = 0;
};

// Portable fallback: target -> backup, replacement -> target, remove backup. If a crash happens in
// between, TagService::removeStaleTempFiles() puts the backup back.
class PortableAtomicReplace final : public IAtomicReplace {
public:
    Result<void> replaceFile(const QString& replacement, const QString& target) override;
};

}  // namespace semplice
