#pragma once

#include "LiveCompareSession.h"

#include <cstdint>
#include <vector>

namespace hypha::live_compare
{
// INV-LC7: the offset between POST's content and PRE as the clock rules map it. The clock rules
// rely on every plug-in between reporting its latency and on the DAW compensating it; nothing in
// the clocks shows a wrong report, the content does. Positive lagFrames: the PRE that matches
// POST lies later than the mapped PRE, so PRE plays late; negative: PRE plays early, as when a
// plug-in between under-reports its latency. Undetermined when the window is too quiet, periodic
// or too heavily processed to show one clear peak; then nothing is claimed either way.
struct OffsetEstimate
{
    bool determined = false;
    std::int64_t lagFrames = 0;
    double peak = 0.0;      // GCC-PHAT peak, 1 for a delayed copy
    double dominance = 0.0; // the peak over the strongest peak away from it
    double sampleRate = 0.0; // the rate lagFrames counts in: the rate the ring is stamped with
};

constexpr std::int64_t offsetSearchFrames = 8192;  // +/-170 ms at 48 kHz
constexpr std::int64_t offsetWindowFrames = 32768; // 0.68 s at 48 kHz

// Non-RT. post holds `frames` mono samples; pre holds frames + 2 * maxLag, its index 0 aligned with
// POST index -maxLag. Cross-correlates with the phase transform (GCC-PHAT), so an EQ or a level
// change between does not move the peak, and polarity does not matter.
OffsetEstimate estimateOffset (const std::vector<float>& post, const std::vector<float>& pre, std::int64_t maxLag);

// Non-RT (message thread): the latest window of POST's input history against PRE's ring through the
// proven K, keeping maxLag of PRE after the window so that PRE has already written it.
OffsetEstimate measureOffset (const Ring&, const PostRenderer&);

// INV-LC7 / LC10, one playback run (play after stop to the next stop). A single estimate never
// decides: two agreeing determined estimates in a row settle the offset that is shown, and the
// first settled offset is the run's baseline. A later settled offset two frames or more away from
// the baseline is a jump: a latency change with unchanged clocks, which holds POST.
class OffsetMonitor
{
public:
    struct Step
    {
        bool settled = false; // lagFrames is the offset of two agreeing estimates
        std::int64_t lagFrames = 0;
        bool jumped = false;  // away from the baseline: hold POST until the run ends
    };

    Step observe (const OffsetEstimate& estimate, bool alreadyHeld) noexcept
    {
        Step step;
        if (! estimate.determined)
            return step;
        const auto distance = [] (std::int64_t a, std::int64_t b) { return a > b ? a - b : b - a; };
        const bool agrees = candidateCount > 0 && distance (estimate.lagFrames, candidate) <= 1;
        candidateCount = agrees ? candidateCount + 1 : 1;
        candidate = estimate.lagFrames;
        if (candidateCount < 2)
            return step;
        step.settled = true;
        step.lagFrames = candidate;
        if (! hasBaseline)
        {
            hasBaseline = true;
            baseline = candidate;
        }
        else if (distance (candidate, baseline) >= 2 && ! alreadyHeld)
        {
            step.jumped = true;
            baseline = candidate;
        }
        return step;
    }

private:
    bool hasBaseline = false;
    int candidateCount = 0;
    std::int64_t baseline = 0, candidate = 0;
};
}
