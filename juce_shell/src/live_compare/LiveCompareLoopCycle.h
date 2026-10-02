#pragma once

#include "LiveCompareBlockClock.h"

#include <cstdint>

namespace hypha::live_compare
{
struct LoopCycleReading
{
    std::int64_t samples = 0;
    bool known = false, changed = false;
};

// Measures the exact native-sample wrap from successive host positions. PPQ only validates that
// the discontinuity was the advertised loop; it is never rounded into a sample length.
class LoopCycleMeter
{
public:
    LoopCycleReading observe (const BlockClock& block, double rate) noexcept
    {
        const bool usable = block.playing && block.clockValid && block.projectValid
            && block.frames > 0 && ! block.afterGap && block.loop.usable (rate);
        if (! usable)
        {
            reset();
            return {};
        }
        if (havePrevious)
        {
            std::int64_t nextClock = 0, linearEnd = 0;
            if (! checkedClockAdd (clock, frames, nextClock)
                || ! checkedClockAdd (project, frames, linearEnd)
                || block.clock != nextClock || ! loop.sameRange (block.loop)
                || ! loopPositionMatches (loop, project, frames, block.loop, block.project, rate))
            {
                reset();
                remember (block);
                return { 0, false, true };
            }
            if (block.project < linearEnd)
            {
                std::int64_t measured = 0;
                if (! checkedClockSubtract (linearEnd, block.project, measured)
                    || measured <= 0 || (known && measured != samples))
                {
                    reset();
                    remember (block);
                    return { 0, false, true };
                }
                samples = measured;
                known = true;
            }
        }
        remember (block);
        return { samples, known, false };
    }

    void reset() noexcept { *this = {}; }

private:
    void remember (const BlockClock& block) noexcept
    {
        clock = block.clock;
        project = block.project;
        frames = block.frames;
        loop = block.loop;
        havePrevious = true;
    }

    std::int64_t clock = 0, project = 0, samples = 0;
    std::int32_t frames = 0;
    LoopContext loop;
    bool havePrevious = false, known = false;
};
}
