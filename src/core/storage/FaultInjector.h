#pragma once

#include <QString>

namespace semplice {

// Test hook: simulates power loss. FileStorageService calls crashHere() at every step of an
// operation ("checkpoint"); if it returns true the operation stops at once, WITHOUT any cleanup,
// and reports E_CRASH_INJECTED. Production code never installs one.
class FaultInjector {
public:
    virtual ~FaultInjector() = default;
    virtual bool crashHere(const QString& checkpoint) = 0;
};

}  // namespace semplice
