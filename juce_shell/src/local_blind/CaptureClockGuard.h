#pragma once

#include <cstdint>
#include <limits>

namespace hypha::local_blind
{
// Raw facts for one role-local Audio Thread callback. Optional presentation latency is used
// only as a change detector. It never supplies an offset or repairs a host-native range.
struct CaptureClockObservation
{
    std::int64_t position = 0;
    int frames = 0;
    std::uint8_t source = 0;
    std::uint8_t presentationSource = 0;
    std::uint32_t inputLatency = 0;
    std::uint32_t outputLatency = 0;
    bool positionValid = false;
    bool timelineActive = false;
    bool bypassed = false;
    bool realtime = false;
    bool hasInputLatency = false;
    bool hasOutputLatency = false;
};

// Audio Thread-owned continuity check from the first callback after arm until completion.
// Missing optional latency is accepted and pinned as missing. A seek, loop wrap, clock-source
// change, latency notification change, stop callback or late first observation rejects the
// request instead of guessing a correction.
class CaptureClockGuard final
{
public:
    CaptureClockGuard (std::uint8_t expectedSourceIn,
                       std::int64_t positionAtIssueIn,
                       std::int64_t nativeStartIn) noexcept
        : expectedSource (expectedSourceIn), positionAtIssue (positionAtIssueIn),
          nativeStart (nativeStartIn) {}

    bool accept (const CaptureClockObservation& value) noexcept
    {
        if (! value.positionValid || ! value.timelineActive || value.bypassed || ! value.realtime
            || value.frames < 1 || value.source != expectedSource
            || value.position > std::numeric_limits<std::int64_t>::max() - value.frames)
            return false;
        if (! initialized)
        {
            if (value.position < positionAtIssue || value.position > nativeStart)
                return false;
            initialized = true;
            presentationSource = value.presentationSource;
            hasInputLatency = value.hasInputLatency;
            hasOutputLatency = value.hasOutputLatency;
            inputLatency = value.inputLatency;
            outputLatency = value.outputLatency;
        }
        else if (value.position != nextPosition
                 || value.presentationSource != presentationSource
                 || value.hasInputLatency != hasInputLatency
                 || value.hasOutputLatency != hasOutputLatency
                 || (hasInputLatency && value.inputLatency != inputLatency)
                 || (hasOutputLatency && value.outputLatency != outputLatency))
            return false;
        nextPosition = value.position + value.frames;
        return true;
    }

private:
    const std::uint8_t expectedSource;
    const std::int64_t positionAtIssue;
    const std::int64_t nativeStart;
    std::int64_t nextPosition = 0;
    std::uint8_t presentationSource = 0;
    std::uint32_t inputLatency = 0, outputLatency = 0;
    bool initialized = false, hasInputLatency = false, hasOutputLatency = false;
};
}
