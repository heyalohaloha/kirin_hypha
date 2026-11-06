#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hypha::hybrid_vu
{
// Unity-DC moving-coil model, in linear rectified amplitude, before the dial mapping.
// 1.25% overswing fixes damping; the first 99% crossing at 300 ms fixes omega.
// y'' + 2*zeta*omega*y' + omega^2*y = omega^2*input.
struct NeedleState
{
    double value = 0.0, velocity = 0.0;
};
struct NeedleStep
{
    static constexpr double zeta = 0.8127169864286775;
    static constexpr double omega = 13.51191256253271;
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0;

    static NeedleStep forSeconds (double seconds) noexcept
    {
        if (! std::isfinite (seconds) || seconds <= 0.0) return {};
        const double damping = zeta * omega;
        const double frequency = omega * std::sqrt (1.0 - zeta * zeta);
        const double decay = std::exp (-damping * seconds);
        const double cosine = decay * std::cos (frequency * seconds);
        const double sine = decay * std::sin (frequency * seconds) / frequency;
        return { cosine + damping * sine, sine, -omega * omega * sine,
                 cosine - damping * sine };
    }
    void apply (NeedleState& state, double input) const noexcept
    {
        const double displacement = state.value - input;
        const double next = input + a * displacement + b * state.velocity;
        state.velocity = c * displacement + d * state.velocity;
        state.value = next;
        // Below the visible floor, and above both float and double subnormal ranges.
        if (std::abs (state.value) < 1.0e-30) state.value = 0.0;
        if (std::abs (state.velocity) < 1.0e-30) state.velocity = 0.0;
    }
};
struct LiveReading
{
    std::array<NeedleState, 2> needles {};
    std::uint64_t generation = 0, wallNanos = 0;
    // The state is at the end of all audio already processed ahead of this callback time.
    double aheadSeconds = 0.0;
    int channels = 0;
    bool feeding = false, coherent = false;
};
inline double needleDbfs (double amplitude) noexcept
{
    return amplitude > 0.0 ? 20.0 * std::log10 (amplitude) : -INFINITY;
}

// Also used to continue the same state on the message thread when callbacks stop.
class NeedleMotion
{
public:
    double position() const noexcept { return state.value; }
    double target() const noexcept { return goal; }
    bool active() const noexcept { return moving; }
    void setTarget (double next) noexcept
    {
        goal = std::isfinite (next) ? std::max (0.0, next) : 0.0;
        moving = state.value != goal || state.velocity != 0.0;
    }
    void restore (NeedleState next) noexcept
    {
        state = next;
        setTarget (0.0);
    }
    void snap (double next) noexcept
    {
        state = { std::isfinite (next) ? std::max (0.0, next) : 0.0, 0.0 };
        goal = state.value;
        moving = false;
    }
    bool advance (double dt) noexcept
    {
        if (! moving || ! std::isfinite (dt) || dt <= 0.0) return false;
        NeedleStep::forSeconds (dt).apply (state, goal);
        if (std::abs (state.value - goal) < 1.0e-12 && std::abs (state.velocity) < 1.0e-11)
            snap (goal);
        return true;
    }
private:
    NeedleState state;
    double goal = 0.0;
    bool moving = false;
};

class NeedleMotionPair
{
public:
    void setVisible (bool next) noexcept
    {
        if (next && ! visible) haveReading = false;
        visible = next;
    }
    void setTarget (int channel, double next) noexcept
    { if (channel >= 0 && channel < 2) needles[channel].setTarget (next); }
    void snap (int channel, double next) noexcept
    { if (channel >= 0 && channel < 2) needles[channel].snap (next); }
    void accept (const LiveReading& reading, std::uint64_t nowNanos = 0) noexcept
    {
        if (! reading.coherent || (haveReading && reading.generation == last.generation)) return;
        last = reading;
        haveReading = fresh = true;
        age = reading.wallNanos != 0 && nowNanos > reading.wallNanos
            ? static_cast<double> (nowNanos - reading.wallNanos) * 1.0e-9 : 0.0;
        restoreAtAge();
    }
    bool active() const noexcept
    { return visible && (fresh || needles[0].active() || needles[1].active()); }
    bool tick (double dt) noexcept
    {
        if (! visible || ! std::isfinite (dt) || dt <= 0.0) return false;
        // A fresh audio state already contains the complete response. Never filter it twice.
        if (fresh) { fresh = false; return true; }
        age += dt;
        if (haveReading) return restoreAtAge();
        const bool left = needles[0].advance (dt), right = needles[1].advance (dt);
        return left || right;
    }
    double position (int channel) const noexcept
    { return channel >= 0 && channel < 2 ? needles[channel].position() : 0.0; }
private:
    bool restoreAtAge() noexcept
    {
        const auto elapsed = std::max (0.0, age - last.aheadSeconds);
        bool changed = false;
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto before = needles[channel].position();
            needles[channel].restore (channel < last.channels ? last.needles[(size_t) channel] : NeedleState {});
            needles[channel].advance (elapsed);
            changed = changed || before != needles[channel].position();
        }
        return changed;
    }
    NeedleMotion needles[2];
    LiveReading last;
    double age = 0.0;
    bool visible = false, haveReading = false, fresh = false;
};
}
