#pragma once
#include <cmath>

namespace hypha::spectrum_display
{
// Presentation only. Input polling and time-based smoothing keep their own clocks.
class FrameCadence
{
public:
    static constexpr double intervalMs = 50.0;
    void reset() noexcept { started = false; }
    bool due (double nowMs) noexcept
    {
        if (! std::isfinite (nowMs) || nowMs < 0.0) { reset(); return false; }
        if (! started || nowMs < lastMs)
        {
            started = true; lastMs = nowMs; nextMs = nowMs + intervalMs;
            return true;
        }
        lastMs = nowMs;
        // Keep the deadline's phase: rounding at 60 Hz must not turn 20 fps into 15.
        constexpr double epsilonMs = 0.000001;
        if (nowMs + epsilonMs < nextMs) return false;
        const auto missed = std::floor ((nowMs - nextMs + epsilonMs) / intervalMs);
        nextMs += (missed + 1.0) * intervalMs;
        // A stall skips expired deadlines, never emits a catch-up burst.
        if (! std::isfinite (nextMs) || nextMs <= nowMs) nextMs = nowMs + intervalMs;
        return true;
    }
private:
    bool started = false;
    double lastMs = 0.0, nextMs = 0.0;
};
}
