#pragma once
#include "LiveCompareRecovery.h"
#include <algorithm>
#include <cstdint>

namespace hypha::live_compare
{
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
