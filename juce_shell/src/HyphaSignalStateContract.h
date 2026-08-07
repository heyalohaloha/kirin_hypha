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
        static constexpr double minimumCallbackGapSeconds = 0.25;
        static constexpr double callbackGapBlockMultiplier = 2.0;

        static constexpr bool eligible (bool bypassed,
                                        bool recording,
                                        bool measurementTimelineActive) noexcept
        {
            return ! bypassed && ! recording && measurementTimelineActive;
        }

        static bool callbackGapStartsNewPass (double callbackGapSeconds,
                                              uint64_t numFrames,
                                              double sampleRate) noexcept
        {
            if (! std::isfinite (callbackGapSeconds) || callbackGapSeconds <= 0.0
                || ! std::isfinite (sampleRate) || sampleRate <= 0.0)
                return false;

            const double blockDurationSeconds = static_cast<double> (numFrames) / sampleRate;
            const double blockThreshold = blockDurationSeconds * callbackGapBlockMultiplier;
            const double threshold = blockThreshold > minimumCallbackGapSeconds
                                   ? blockThreshold : minimumCallbackGapSeconds;
            return callbackGapSeconds > threshold;
        }

        bool observeBlock (bool watchTimelineEligible,
                           bool callbackGapStartedNewPass,
                           bool silent,
                           uint64_t numFrames,
                           double sampleRate) noexcept
        {
            // Studio Pro may stop processBlock entirely while transport/bypass is stopped. In
            // that case there is no ineligible callback to reset this audio-thread-local gate.
            // A bounded callback gap is therefore an explicit Watch pass boundary.
            if (callbackGapStartedNewPass)
                reset();

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
