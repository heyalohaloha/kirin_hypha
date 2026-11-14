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
//  - It never falls below the last received value or the visible line.
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
        held.fill (-std::numeric_limits<float>::infinity());
        latest = held;
        heldAt.fill (0.0);
        clockMs = 0.0;
        have = false;
    }
    bool empty() const noexcept { return ! have; }
    const Values& values() const noexcept { return held; }

    // `received` is the value as it will be drawn once the line has caught up; `visible` is the
    // line on screen now. Time already passed is settled first, then the new maxima are taken.
    void accept (const Values& received, const Values& visible, double nowMs) noexcept
    {
        advance (visible, nowMs);
        const double at = time (nowMs);
        for (std::size_t index = 0; index < Size; ++index)
        {
            if (! std::isfinite (received[index])) continue;
            latest[index] = received[index];
            if (! (received[index] < held[index])) { held[index] = received[index]; heldAt[index] = at; }
        }
        if (! have) clockMs = at;
        have = true;
    }

    // Returns whether the line moved. A line resting on its floor reports no movement, so a
    // settled display is not repainted.
    bool advance (const Values& visible, double nowMs) noexcept
    {
        const double at = time (nowMs);
        if (! have || ! (at > clockMs)) return false;
        bool moved = false;
        for (std::size_t index = 0; index < Size; ++index)
        {
            const double from = std::max (clockMs, heldAt[index] + holdMs);
            if (! std::isfinite (held[index]) || ! (at > from)) continue;
            const float floor = std::isfinite (visible[index]) ? std::max (latest[index], visible[index]) : latest[index];
            const float next = std::max (floor, static_cast<float> (
                static_cast<double> (held[index]) - (at - from) * fallDbPerSecond / 1000.0));
            if (next < held[index]) { held[index] = next; moved = true; }
        }
        clockMs = at;
        return moved;
    }

    void pause (const Values& visible, double nowMs) noexcept
    {
        if (paused) return;
        advance (visible, nowMs);
        paused = true; pausedAtMs = nowMs;
    }
    void resume (double nowMs) noexcept
    {
        if (! paused) return;
        paused = false;
        const double stood = std::max (0.0, nowMs - pausedAtMs);
        for (auto& at : heldAt) at += stood;
        clockMs += stood;
    }
    bool isPaused() const noexcept { return paused; }

private:
    double time (double nowMs) const noexcept { return paused ? pausedAtMs : nowMs; }
    Values held {}, latest {};
    std::array<double, Size> heldAt {};
    double clockMs = 0.0, pausedAtMs = 0.0;
    bool have = false, paused = false;
};
}
