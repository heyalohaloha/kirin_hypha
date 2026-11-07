#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace hypha::live_compare
{
// The PRE gain on its way to a new MATCH or AUTO value (plan 6.2): a linear ramp over 50 ms, so a
// gain that changes while PRE sounds never steps. While PRE is silent it moves at once.
class GainRamp
{
public:
    void configure (double sampleRate) noexcept
    {
        rampFrames = std::max (1, static_cast<int> (sampleRate * 0.05 + 0.5));
        settle (1.0f);
    }

    // Audio Thread: the gain for the next frame on the way to wanted. Each frame is placed on the
    // line from the ramp's start, so no rounding accumulates into a step.
    float next (float wanted) noexcept
    {
        if (std::fabs (wanted - target) > 0.0f)
        {
            start = current;
            target = wanted;
            position = 0;
        }
        if (position < rampFrames)
        {
            ++position;
            current = position >= rampFrames
                ? target
                : start + (target - start) * (static_cast<float> (position) / static_cast<float> (rampFrames));
        }
        return current;
    }

    // Audio Thread: PRE is silent, nothing hears the gain move.
    void settle (float wanted) noexcept
    {
        start = current = target = wanted;
        position = rampFrames;
    }

    // The largest gain the next block reaches: a rising ramp is checked at its end.
    float peak (float wanted) const noexcept { return std::max (current, wanted); }

    bool settledAt (float wanted) const noexcept
    {
        return position >= rampFrames && current >= wanted && current <= wanted
            && target >= wanted && target <= wanted;
    }

private:
    float start = 1.0f, current = 1.0f, target = 1.0f; // Audio Thread only
    int rampFrames = 2400, position = 2400;
};

}
