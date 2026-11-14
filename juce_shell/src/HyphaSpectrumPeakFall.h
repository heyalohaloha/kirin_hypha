#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace hypha::spectrum_presentation
{
// The opt-in PEAK line that stays, then falls slowly. Values are dB, before RANGE and TILT.
//  - A maximum and its time are recorded when a value is received, never from the drawn
//    interpolation, so the line does not depend on the frame rate or on a resting display.
//  - It stays for holdMs from that time, then falls at fallDbPerSecond. The fall is a function
//    of time only: frames that were not drawn (hidden, resting) are made up on the next call.
//    The remembered maximum and its time are kept apart from the drawn height, so a line that
//    was resting on the curve, or a stale curve after a stall, never hides time that has passed.
//  - The drawn height is never below the last received value or the visible line, from the
//    first value on: a line started over while the curve is still falling starts on the curve.
//  - While paused the clock stands still and the line remains.
template <std::size_t Size>
class PeakFall
{
public:
    using Values = std::array<float, Size>;
    static constexpr double holdMs = 1500.0;
    static constexpr double fallDbPerSecond = 4.0;

    PeakFall() noexcept { clear(); }

    void clear() noexcept
    {
        shown.fill (-std::numeric_limits<float>::infinity());
        latest = maximum = shown;
        maximumAt.fill (0.0);
        have = false;
    }
    bool empty() const noexcept { return ! have; }
    const Values& values() const noexcept { return shown; }

    // `received` is the value as it will be drawn once the line has caught up; `visible` is the
    // line on screen now (it may be stale: it is only a floor, never a clock).
    void accept (const Values& received, const Values& visible, double nowMs) noexcept
    {
        const double at = time (nowMs);
        for (std::size_t index = 0; index < Size; ++index)
        {
            if (! std::isfinite (received[index])) continue;
            latest[index] = received[index];
            if (! (received[index] < fallen (index, at))) { maximum[index] = received[index]; maximumAt[index] = at; }
        }
        have = true;
        place (visible, at);
    }

    // Returns whether the drawn line moved. A line resting on its floor reports no movement, so a
    // settled display is not repainted.
    bool advance (const Values& visible, double nowMs) noexcept
    { return have && place (visible, time (nowMs)); }

    void pause (const Values& visible, double nowMs) noexcept
    {
        if (paused) return;
        advance (visible, nowMs);
        paused = true; pausedAtMs = std::max (nowMs, clockMs);
    }
    void resume (double nowMs) noexcept
    {
        if (! paused) return;
        paused = false;
        const double stood = std::max (0.0, nowMs - pausedAtMs);
        for (auto& at : maximumAt) at += stood;
        clockMs += stood;
    }
    bool isPaused() const noexcept { return paused; }

private:
    // Time never runs backwards, whichever caller's clock reading is the older one.
    double time (double nowMs) noexcept { return clockMs = std::max (clockMs, paused ? pausedAtMs : nowMs); }
    // The remembered maximum after its hold and fall: a function of the time alone.
    double fallen (std::size_t index, double at) const noexcept
    {
        return static_cast<double> (maximum[index])
             - std::max (0.0, at - maximumAt[index] - holdMs) * fallDbPerSecond / 1000.0;
    }
    bool place (const Values& visible, double at) noexcept
    {
        bool moved = false;
        for (std::size_t index = 0; index < Size; ++index)
        {
            if (! std::isfinite (latest[index])) continue;
            const float floor = std::isfinite (visible[index]) ? std::max (latest[index], visible[index]) : latest[index];
            const float next = std::max (floor, static_cast<float> (fallen (index, at)));
            if (next != shown[index]) { shown[index] = next; moved = true; }
        }
        return moved;
    }
    Values shown {}, latest {}, maximum {};
    std::array<double, Size> maximumAt {};
    double clockMs = 0.0, pausedAtMs = 0.0;
    bool have = false, paused = false;
};
}
