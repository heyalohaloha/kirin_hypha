#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "PreDisplayClock.h"

namespace hypha::pre_display
{
    struct DisplaySnapshot;
    struct GuideModel;

    inline bool projectSamplesToNanoseconds (std::int64_t samples, double sampleRate,
                                             std::int64_t& nanoseconds) noexcept
    {
        if (! std::isfinite (sampleRate) || sampleRate < 8'000.0 || sampleRate > 768'000.0)
            return false;
        const long double scaled = static_cast<long double> (samples) * 1'000'000'000.0L
                                 / static_cast<long double> (sampleRate);
        if (scaled < static_cast<long double> (std::numeric_limits<std::int64_t>::min())
            || scaled > static_cast<long double> (std::numeric_limits<std::int64_t>::max()))
            return false;
        nanoseconds = static_cast<std::int64_t> (std::llround (scaled));
        return true;
    }

    constexpr bool containsHalfOpen (std::int64_t start, std::int64_t end,
                                     std::int64_t position) noexcept
    {
        return position >= start && position < end;
    }

    constexpr bool subtractNanoseconds (std::int64_t projectNanoseconds,
                                        std::int64_t sourceZeroProjectNanoseconds,
                                        std::int64_t& sourceNanoseconds) noexcept
    {
        if ((sourceZeroProjectNanoseconds > 0
             && projectNanoseconds < std::numeric_limits<std::int64_t>::min()
                                         + sourceZeroProjectNanoseconds)
            || (sourceZeroProjectNanoseconds < 0
                && projectNanoseconds > std::numeric_limits<std::int64_t>::max()
                                            + sourceZeroProjectNanoseconds))
            return false;
        sourceNanoseconds = projectNanoseconds - sourceZeroProjectNanoseconds;
        return true;
    }

    DisplaySnapshot projectDisplay (const GuideModel& guide, const ClockSnapshot& clock,
                                    std::int64_t clockObservedAtMs, std::int64_t nowMs);
}
