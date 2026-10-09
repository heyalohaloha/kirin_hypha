#pragma once
#include "HyphaAttackV2Model.h"
#include <algorithm>
#include <cmath>

namespace hypha::attack_v2
{
// Display-only sample clock. Arrivals update the fact ceiling, never the moving anchor.
// The provisional 150ms look-behind is injectable for development calibration, not a DAW guarantee.
class ViewportClock
{
public:
    void configure (double lookBehindMs, double freshnessMs) noexcept
    { lag = std::max (0.0, lookBehindMs); freshness = std::max (lag, freshnessMs); reset(); }
    void reset() noexcept { state = ClockState::initial; cutoff = viewport = -1; anchored = false; rate = 0; }
    void observe (std::int64_t c, std::uint32_t sampleRate, double now, bool active, bool realtime) noexcept
    {
        if (! std::isfinite (now) || c < 0 || sampleRate == 0) { reset(); return; }
        if (sampleRate != rate || c < cutoff) reset();
        const bool progressed = c > cutoff;
        rate = sampleRate; cutoff = c;
        if (progressed) factTime = now;
        if (! active || ! realtime)
        { if (viewport < 0) viewport = c; state = ClockState::hold; anchored = false; return; }
        if (state == ClockState::hold && ! progressed) return;
        if (! anchored)
        {
            const auto behind = static_cast<std::int64_t> (lag * rate / 1000.0);
            if (c < behind) { viewport = 0; state = ClockState::initial; return; }
            anchorSample = c - behind; anchorTime = now; viewport = anchorSample;
            anchored = true; state = ClockState::moving;
        }
        tick (now);
    }
    void tick (double now) noexcept
    {
        if (! anchored || state != ClockState::moving || ! std::isfinite (now)) return;
        const auto desired = anchorSample + static_cast<std::int64_t> (
            std::max (0.0, now - anchorTime) * rate / 1000.0);
        if (now - factTime >= freshness || desired >= cutoff)
        {
            viewport = std::min (desired, cutoff); state = ClockState::hold; anchored = false;
        }
        else viewport = desired;
    }
    ClockState status() const noexcept { return state; }
    std::int64_t position() const noexcept { return viewport; }
    std::int64_t ceiling() const noexcept { return cutoff; }
private:
    double lag = 150, freshness = 250, anchorTime = 0, factTime = 0;
    std::int64_t anchorSample = 0, cutoff = -1, viewport = -1;
    std::uint32_t rate = 0;
    bool anchored = false;
    ClockState state = ClockState::initial;
};
}
