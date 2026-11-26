#pragma once

#include <QDateTime>

namespace semplice {

// Injectable time source so tests are deterministic.
class IClock {
public:
    virtual ~IClock() = default;
    virtual QDateTime now() const = 0;
};

class SystemClock final : public IClock {
public:
    QDateTime now() const override { return QDateTime::currentDateTime(); }
};

}  // namespace semplice
