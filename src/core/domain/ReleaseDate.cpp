#include "core/domain/ReleaseDate.h"

#include <QDate>

namespace semplice {

Result<void> ReleaseDate::validate(int currentYear) const
{
    if (year < kMinYear || year > currentYear + 1)
        return Error::make(ErrorCode::InvalidDate, QStringLiteral("year out of range"));
    if (month < 0 || month > 12)
        return Error::make(ErrorCode::InvalidDate, QStringLiteral("month out of range"));
    if (day != 0 && month == 0)
        return Error::make(ErrorCode::InvalidDate, QStringLiteral("day without month"));
    if (day != 0 && !QDate::isValid(year, month, day))
        return Error::make(ErrorCode::InvalidDate, QStringLiteral("no such day"));
    return {};
}

int ReleaseDate::sortKey() const
{
    return year * 10000 + (month == 0 ? 1 : month) * 100 + (day == 0 ? 1 : day);
}

}  // namespace semplice
