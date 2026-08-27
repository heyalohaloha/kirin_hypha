#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "PreDisplayClock.h"

namespace hypha::pre_display
{
    constexpr std::int64_t projectionPollMs = 100;
    constexpr std::int64_t minimumCueWindowNs = 500'000'000;
    constexpr std::int64_t inspectHoldWindowNs = 1'000'000'000;
    static_assert (minimumCueWindowNs >= projectionPollMs * 4 * 1'000'000,
                   "A short PRE fact must span several worker/UI presentation samples");

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

    constexpr std::int64_t saturatingAddNanoseconds (std::int64_t value,
                                                      std::int64_t delta) noexcept
    {
        if (delta > 0 && value > std::numeric_limits<std::int64_t>::max() - delta)
            return std::numeric_limits<std::int64_t>::max();
        if (delta < 0 && value < std::numeric_limits<std::int64_t>::min() - delta)
            return std::numeric_limits<std::int64_t>::min();
        return value + delta;
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
