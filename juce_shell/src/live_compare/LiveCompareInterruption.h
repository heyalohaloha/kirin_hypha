#pragma once
#include "LiveCompareRecovery.h"

namespace hypha::live_compare
{
// One priority order for active anonymous output and an established preparation. A DC
// notification may reset the observer, but must never disguise an actual callback hole.
struct Interruption
{
    RecoveryReason authority = RecoveryReason::none;
    bool bypassed = false, offline = false, usable = true, outputTaken = false;
    bool playing = true, projectValid = true, clockValid = true, callbackGap = false;
    bool compensationOff = false, contentHeld = false;
    RecoveryReason reason (RecoveryReason timingLoss = RecoveryReason::none) const noexcept
    {
        using R = RecoveryReason;
        if (authority != R::none) return authority;
        if (bypassed) return R::bypassed;
        if (offline) return R::offline;
        if (! usable) return R::formatChanged;
        if (outputTaken) return R::outputTaken;
        if (! playing) return R::stopped;
        if (! projectValid) return R::projectClockMissing;
        if (! clockValid) return R::clockMissing;
        if (callbackGap) return R::callbackGap;
        if (compensationOff) return R::compensationOff;
        if (contentHeld) return R::contentChanged;
        return timingLoss;
    }
};
}
