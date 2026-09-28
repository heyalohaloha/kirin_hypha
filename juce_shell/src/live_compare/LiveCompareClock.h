#pragma once

#include "../HostAuxiliaryClock.h"

#include <cstdint>

namespace hypha::live_compare
{
// Which continuous clock a side uses as the ring index. VST3 continuous time and AU render time
// come from the host. AAX has no loop-free host clock (its native sample location folds at loop
// ends), so an AAX side counts its own frames; that clock, like AU render time, advances only
// while the host calls the instance, and the gap rule covers the calls it skips.
enum class ClockBasis : std::uint8_t
{
    hostContinuous,
    pluginFrames
};

constexpr ClockBasis clockBasisFor (AuxiliaryClockSource source) noexcept
{
    return source == AuxiliaryClockSource::vst3Continuous || source == AuxiliaryClockSource::audioUnitRender
               ? ClockBasis::hostContinuous
               : ClockBasis::pluginFrames;
}

struct ContinuousReading
{
    std::int64_t samples = 0;
    bool valid = false;
};

// Audio Thread. One per side; never shared between PRE and POST.
class ContinuousClock
{
public:
    ContinuousReading next (const HostAuxiliaryClock& auxiliary, std::int32_t frames) noexcept
    {
        ContinuousReading reading;
        if (frames <= 0)
            return reading;
        if (clockBasisFor (auxiliary.source) == ClockBasis::hostContinuous)
        {
            reading.samples = auxiliary.samples;
            reading.valid = auxiliary.valid;
            return reading;
        }
        reading.samples = pluginFrames;
        reading.valid = true;
        pluginFrames += frames;
        return reading;
    }

private:
    std::int64_t pluginFrames = 0;
};

// Host-profile values for the callback gap. The initial values come from Studio Pro 8.1.2 and
// Pro Tools 2026.4 (regular callbacks, gaps of seconds); each host and buffer setting qualifies
// its own values before a profile ships.
struct GapProfile
{
    double factor = 2.5;          // a gap is longer than this many previous block durations
    double floorSeconds = 0.020;  // and never shorter than this
};

// Audio Thread. Reports whether the wall-clock time since the previous callback means the host
// skipped this side (plug-in sleep, stop), which invalidates any continuity across it.
class GapDetector
{
public:
    explicit GapDetector (GapProfile profileIn = {}) noexcept : profile (profileIn) {}

    bool observe (std::uint64_t nowNanos, std::int32_t frames, double sampleRate) noexcept
    {
        bool gap = false;
        if (havePrevious && previousFrames > 0 && sampleRate > 0.0 && nowNanos >= previousNanos)
        {
            const double expected = static_cast<double> (previousFrames) / sampleRate * 1.0e9;
            const double threshold = expected * profile.factor > profile.floorSeconds * 1.0e9
                                         ? expected * profile.factor
                                         : profile.floorSeconds * 1.0e9;
            gap = static_cast<double> (nowNanos - previousNanos) > threshold;
        }
        havePrevious = true;
        previousNanos = nowNanos;
        previousFrames = frames;
        return gap;
    }

    void reset() noexcept { havePrevious = false; }

private:
    GapProfile profile;
    bool havePrevious = false;
    std::uint64_t previousNanos = 0;
    std::int32_t previousFrames = 0;
};
}
