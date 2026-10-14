#pragma once
#include "LiveTimingFixtureAccess.h"
#include <stdexcept>

namespace hypha::tests
{
// Sole producer, OUTSIDE processBlock. Receipt is deliberately not an output oracle: every
// existing delta, warmup, drop, bit-identity and PCM assertion still runs after this handoff.
inline void awaitAnalysisInput (KirinHyphaProcessorBase& processor)
{
    if (!LiveTimingFixtureAccess::waitAnalysisInput (processor, 5000))
        throw std::runtime_error ("analysis worker did not complete queued input");
}
inline void awaitAnalysisExchange (KirinHyphaProcessorBase& processor, bool endpoint = false)
{
    if (!LiveTimingFixtureAccess::waitAnalysisExchange (processor, 5000, endpoint))
        throw std::runtime_error ("analysis exchange did not complete its tick");
}
}
