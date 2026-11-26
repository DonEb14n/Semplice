#pragma once

#include "core/util/Result.h"

namespace semplice {

// Year is required; month and day are optional (0 = "-" in the UI). SPEC 5.5.
struct ReleaseDate {
    int year = 0;
    int month = 0;
    int day = 0;

    static constexpr int kMinYear = 1744;

    // `currentYear` is injected (IClock) so tests are deterministic. Allowed years: 1744..currentYear+1.
    // Invalid: missing year, month outside 1..12, day without month, day not in that month.
    Result<void> validate(int currentYear) const;

    // Sorting key: missing month = January, missing day = 1st. yyyymmdd.
    int sortKey() const;

    bool operator==(const ReleaseDate&) const = default;
};

}  // namespace semplice
