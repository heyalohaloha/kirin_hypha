#pragma once

#include <algorithm>
#include <cmath>

namespace hypha::hybrid_vu
{
// The state is in dial coordinates, not decibels. Updates run on the UI thread.
class NeedleMotion
{
public:
    double position() const noexcept { return value; }
    double target() const noexcept { return goal; }
    bool active() const noexcept { return moving; }

    // Live: the target arrives every frame from the audio thread's own 300 ms integration
    // (HyphaVuLiveTap.h). The follower then keeps its momentum across the small per-frame
    // changes and uses a faster response, so it trails the integrated value by about 2/omega
    // (14 ms) instead of restarting a 100 ms response at every publication. The clamp in
    // advance() still forbids crossing the target, so it never overshoots.
    void setLive (bool next) noexcept { live = next; }
    bool isLive() const noexcept { return live; }

    void setTarget (double next) noexcept
    {
        next = std::isfinite (next) ? std::clamp (next, 0.0, 1.0) : 0.0;
        if (next == goal) return;
        goal = next;
        // Starting a new critically damped response without inherited momentum keeps
        // reversals and smaller successive targets from crossing the new target.
        if (! live) velocity = 0.0;
        moving = value != goal;
    }

    void snap (double next) noexcept
    {
        goal = value = std::isfinite (next) ? std::clamp (next, 0.0, 1.0) : 0.0;
        velocity = 0.0;
        moving = false;
    }

    bool advance (double dt) noexcept
    {
        if (! moving || ! std::isfinite (dt) || dt <= 0.0) return false;
        if (dt > 0.25) { snap (goal); return true; }
        const double omega = live ? liveOmega : steppedOmega;
        const auto displacement = value - goal;
        const auto term = velocity + omega * displacement;
        const auto decay = std::exp (-omega * dt);
        const auto next = goal + (displacement + term * dt) * decay;
        velocity = (velocity - omega * term * dt) * decay;
        value = std::clamp (next, std::min (value, goal), std::max (value, goal));
        if (std::abs (value - goal) < 1.0e-5 && std::abs (velocity) < 1.0e-3)
            snap (goal);
        return true;
    }

private:
    static constexpr double steppedOmega = 67.0; // 100 ms publication steps: settles within one step
    static constexpr double liveOmega = 140.0;   // per-frame live feed: group delay 2/omega = 14 ms
    double value = 0.0, goal = 0.0, velocity = 0.0;
    bool moving = false, live = false;
};

// The owner controls attachment lifetime; this object gates frame work while hidden or idle.
class NeedleMotionPair
{
public:
    void setVisible (bool next) noexcept { visible = next; }
    void setLive (bool next) noexcept { for (auto& needle : needles) needle.setLive (next); }
    void setTarget (int channel, double next) noexcept
    {
        if (channel >= 0 && channel < 2) needles[channel].setTarget (next);
    }
    void snap (int channel, double next) noexcept
    {
        if (channel >= 0 && channel < 2) needles[channel].snap (next);
    }
    bool active() const noexcept
    {
        return visible && (needles[0].active() || needles[1].active());
    }
    bool tick (double dt) noexcept
    {
        if (! active()) return false;
        const auto left = needles[0].advance (dt);
        const auto right = needles[1].advance (dt);
        return left || right;
    }
    double position (int channel) const noexcept
    {
        return channel >= 0 && channel < 2 ? needles[channel].position() : 0.0;
    }
private:
    NeedleMotion needles[2];
    bool visible = false;
};
}
