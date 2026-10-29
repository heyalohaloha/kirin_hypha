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

    void setTarget (double next) noexcept
    {
        next = std::isfinite (next) ? std::clamp (next, 0.0, 1.0) : 0.0;
        if (next == goal) return;
        goal = next;
        // Starting a new critically damped response without inherited momentum keeps
        // reversals and smaller successive targets from crossing the new target.
        velocity = 0.0;
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
        constexpr double omega = 67.0;
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
    double value = 0.0, goal = 0.0, velocity = 0.0;
    bool moving = false;
};

// The owner controls attachment lifetime; this object gates frame work while hidden or idle.
class NeedleMotionPair
{
public:
    void setVisible (bool next) noexcept { visible = next; }
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
