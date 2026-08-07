#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace hypha::signal_state_contract
{
    // Watch must not treat a short musical rest as a transport stop. Keep the state active
    // while the exact LUFS-S window can still contain audible samples; after one full 3 s
    // Short-term window of silence, Inactive is again truthful.
    class WatchSilenceGate
    {
    public:
        static constexpr bool eligible (bool bypassed,
                                        bool recording,
                                        bool measurementTimelineActive) noexcept
        {
            return ! bypassed && ! recording && measurementTimelineActive;
        }

        bool observeBlock (bool watchTimelineEligible,
                           bool silent,
                           uint64_t numFrames,
                           double sampleRate) noexcept
        {
            if (! watchTimelineEligible || ! std::isfinite (sampleRate) || sampleRate <= 0.0)
            {
                reset();
                return false;
            }

            if (! silent)
            {
                heardAudibleSignal = true;
                silentFrames = 0;
                return true;
            }

            if (! heardAudibleSignal)
                return false;

            const auto remaining = std::numeric_limits<uint64_t>::max() - silentFrames;
            silentFrames += numFrames > remaining ? remaining : numFrames;
            if (silentFrames >= silenceWindowFrames (sampleRate))
            {
                reset();
                return false;
            }
            return true;
        }

        void reset() noexcept
        {
            silentFrames = 0;
            heardAudibleSignal = false;
        }

        static uint64_t silenceWindowFrames (double sampleRate) noexcept
        {
            constexpr double shortTermWindowSeconds = 3.0;
            const double frames = sampleRate * shortTermWindowSeconds;
            if (frames >= static_cast<double> (std::numeric_limits<uint64_t>::max()))
                return std::numeric_limits<uint64_t>::max();
            return frames <= 1.0 ? 1 : static_cast<uint64_t> (frames);
        }

    private:
        uint64_t silentFrames = 0;
        bool heardAudibleSignal = false;
    };
}
