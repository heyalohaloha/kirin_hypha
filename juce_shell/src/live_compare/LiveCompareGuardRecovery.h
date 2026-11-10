#pragma once
#include "LiveCompareRecovery.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace hypha::live_compare
{
// The largest PRE sample a raised gain may carry under the ceiling: rounded down, never up, so a
// float threshold cannot admit a sample whose gained value passes the ceiling. Unlimited at unity.
inline float ceilingSampleLimit (float preGain, float ceilingLinear) noexcept
{
    if (! (preGain > 1.0f)) return std::numeric_limits<float>::infinity();
    const double exact = static_cast<double> (ceilingLinear) / static_cast<double> (preGain);
    auto limit = static_cast<float> (exact);
    if (static_cast<double> (limit) > exact) limit = std::nextafter (limit, 0.0f);
    return limit;
}

// A level fault is one episode, not a fresh source switch on every safe callback.
// Recover after continuous safe audio spans the fault block and the existing gain-ramp
// interval. No wall timer, selection change, gain change, or timing permission lives here.
class GuardRecovery
{
public:
    void configure (double rate) noexcept
    { rampFrames = std::max<std::int64_t> (1, static_cast<std::int64_t> (rate * 0.05 + 0.5)); reset(); }
    void reset() noexcept { reason = RecoveryReason::none; safe = required = 0; }
    void discontinuity() noexcept { safe = 0; }
    // A BLIND trial answers the ceiling by matching on the quieter side, so an episode latched
    // earlier (in LISTEN) no longer applies; a non-finite episode stays.
    void releaseCeiling() noexcept { if (reason == RecoveryReason::ceiling) reset(); }
    RecoveryReason observe (RecoveryReason fault, int frames) noexcept
    {
        if (fault != RecoveryReason::none)
        { reason = fault; required = std::max<std::int64_t> (required, frames + rampFrames); safe = 0; }
        else if (reason != RecoveryReason::none)
        {
            safe += frames;
            if (safe >= required) reset();
        }
        return reason;
    }
private:
    RecoveryReason reason = RecoveryReason::none;
    std::int64_t rampFrames = 2400, required = 0, safe = 0;
};
}
