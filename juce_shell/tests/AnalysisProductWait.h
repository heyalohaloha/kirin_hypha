#pragma once
#include <chrono>
#include <cstdint>

namespace hypha::tests
{
using AnalysisTestClock = std::chrono::steady_clock;
enum class AnalysisWaitResult { ready, audioLimit, wallLimit };

constexpr std::uint64_t analysisWaitBlocks (std::uint64_t rate, std::uint64_t frames)
{
    // Twelve seconds of INPUT, including the 96 kHz fixture. Scheduler speed must not
    // decide how many apertures, calibration blocks or MATCH windows a test supplies.
    return (12 * rate + frames - 1) / frames;
}

inline AnalysisTestClock::time_point analysisPhaseDeadline (AnalysisTestClock::time_point started,
                                                           int ctestTimeoutSeconds)
{
    // One deadline for setup and ALL waits/checks in a CTest item, leaving time for diagnostics.
    return started + std::chrono::seconds (ctestTimeoutSeconds - 15);
}

template <class Block, class Check, class Now>
AnalysisWaitResult waitForAnalysis (std::uint64_t blocks, AnalysisTestClock::time_point end,
                                    Block block, Check check, Now now)
{
    for (std::uint64_t i = 0; i < blocks && now() < end; ++i)
    {
        block();
        if (now() >= end) return AnalysisWaitResult::wallLimit;
        if (check()) return AnalysisWaitResult::ready;
    }
    return now() >= end ? AnalysisWaitResult::wallLimit : AnalysisWaitResult::audioLimit;
}

inline AnalysisTestClock::time_point analysisBlockDeadline (AnalysisTestClock::time_point start)
{
    // Leave workers a scheduling opportunity without adding 5 ms on top of the work.
    // Start afresh for EVERY block; never burst through overdue blocks to catch up.
    return start + std::chrono::milliseconds (5);
}
}
