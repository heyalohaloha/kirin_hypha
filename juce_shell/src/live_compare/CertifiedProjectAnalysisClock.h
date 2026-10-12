#pragma once
#include "PairedAnalysisClock.h"

namespace hypha::live_compare
{
// Measurement only. A host-specific certificate says project samples already name the input
// content (K = 0). Never add output latency again. This is NOT a continuous audition clock and
// cannot identify which loop occurrence to play. The exchange still requires complete matching
// PRE/POST windows, the selected PRE owner, and a current request/response generation.
class CertifiedProjectAnalysisClock
{
public:
    AnalysisClockReading observe (const BlockClock& block, double rate, bool eligible) noexcept
    {
        const bool valid = eligible && block.projectValid && block.playing && block.frames > 0
            && ! block.afterGap && std::isfinite (rate) && rate > 0
            && block.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::certifiedProject);
        std::int64_t next = 0, end = 0;
        const bool continuous = havePrevious && valid && sameClockProof (previous, block)
            && (rate <= previousRate && rate >= previousRate) && checkedClockAdd (previous.project, previous.frames, next)
            && next == block.project && previous.loop.sameRange (block.loop);
        // Wraps, position moves and changed notifications retire the old aperture, including a
        // wrap that lands on the same analysis grid. No analysis window crosses a discontinuity.
        if (! continuous)
        {
            if (revision >= (std::uint64_t (1) << 60) - 1) exhausted = true;
            else ++revision;
        }
        previous = block; previousRate = rate; havePrevious = valid;
        if (! valid || exhausted || ! checkedClockAdd (block.project, block.frames, end)) return {};
        // The first block starts a new aperture. On an observed move, drop that block as well so
        // a cached difference expires synchronously before workers process the new coordinate.
        const bool moved = wasValid && ! continuous;
        wasValid = valid;
        if (moved) return {};
        return { true, block.project, revision };
    }
private:
    BlockClock previous;
    double previousRate = 0;
    std::uint64_t revision = 0;
    bool havePrevious = false, wasValid = false, exhausted = false;
};
}
