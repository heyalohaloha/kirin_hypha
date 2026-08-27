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

        // A wall-clock callback delay is not a transport fact: CPU overload and offline render
        // scheduling can pause processBlock while the musical sample timeline remains continuous.
        // Start a new Watch pass only when the host-provided sample range is discontinuous.
        static constexpr bool sampleTimelineStartsNewPass (bool hasPosition,
                                                           bool previousPositionValid,
                                                           int64_t positionSamples,
                                                           int64_t previousPositionSamples,
                                                           uint64_t previousNumFrames) noexcept
        {
            if (! hasPosition || ! previousPositionValid
                || previousNumFrames > static_cast<uint64_t> (std::numeric_limits<int64_t>::max()))
                return false;
            const auto frames = static_cast<int64_t> (previousNumFrames);
            if (previousPositionSamples > std::numeric_limits<int64_t>::max() - frames)
                return true;
            return positionSamples != previousPositionSamples + frames;
        }

        bool observeBlock (bool watchTimelineEligible,
                           bool sampleTimelineStartedNewPass,
                           bool silent,
                           uint64_t numFrames,
                           double sampleRate) noexcept
        {
            if (sampleTimelineStartedNewPass)
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

    /// The shared heartbeat evaluator distinguishes a stopped processor from a scheduling gap.
    /// Start a pass only when that shared state returns from unavailable to Active.
    static constexpr bool availabilityStartsNewPass (uint8_t previousSignalState,
                                                      uint8_t currentSignalState,
                                                      bool recording) noexcept
    {
        constexpr uint8_t inactive = 0;
        constexpr uint8_t active = 1;
        constexpr uint8_t bypassed = 2;
        return ! recording
            && (previousSignalState == inactive || previousSignalState == bypassed)
            && currentSignalState == active;
    }
}
