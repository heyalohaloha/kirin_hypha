#pragma once
#include "LiveTimingFixtureAccess.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace hypha::tests
{
// Sole producer, OUTSIDE processBlock. Receipt is deliberately not an output oracle: every
// existing delta, warmup, drop, bit-identity and PCM assertion still runs after this handoff.
inline void awaitAnalysisInput (KirinHyphaProcessorBase& processor)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (! LiveTimingFixtureAccess::analysisInputDrained (processor))
    {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error ("analysis worker did not receive queued input");
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}
}
