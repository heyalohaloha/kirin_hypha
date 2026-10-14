#pragma once

#include "LiveCompareSession.h"

#include <cstdint>
#include <cmath>
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
    std::int64_t clockK = 0;
    std::int32_t blockFrames = 0;
    std::uint64_t stopGeneration = 0, timelineGeneration = 0;
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

// INV-LC10: time tolerances, independent of rate. GCC-PHAT's display threshold is too
// permissive for a safety hold after clipping. Only a strong, unambiguous peak votes here.
constexpr double offsetJumpSeconds = 0.00025; // 0.25 ms: below this, content is not latency proof
constexpr double offsetAgreementSeconds = 0.00005; // 0.05 ms between consecutive estimates
constexpr double offsetJumpPeak = 0.20, offsetJumpDominance = 8.0;
inline bool offsetCanVote (const OffsetEstimate& e) noexcept
{
    return e.determined && std::isfinite (e.sampleRate) && e.sampleRate >= 8000.0 && e.sampleRate <= 768000.0
        && std::isfinite (e.peak) && e.peak >= offsetJumpPeak
        && ! std::isnan (e.dominance) && e.dominance >= offsetJumpDominance;
}

// One stop-to-stop run. A weak/ambiguous window breaks the consecutive evidence. It cannot
// establish or move a baseline. Correlation remains a warning/hold, never PRE timing authority.
class OffsetMonitor
{
public:
    struct Step
    {
        bool settled = false;
        std::int64_t lagFrames = 0;
        bool jumped = false, recovered = false;
        double sampleRate = 0.0;
    };
    Step observe (const OffsetEstimate& estimate, bool alreadyHeld) noexcept
    {
        Step step;
        if (! offsetCanVote (estimate)) { candidateCount = 0; return step; }
        if (std::abs (rate - estimate.sampleRate) > 0.0) { *this = {}; rate = estimate.sampleRate; }
        const auto distance = [] (std::int64_t a, std::int64_t b)
        { return std::abs (static_cast<long double> (a) - static_cast<long double> (b)); };
        const bool agrees = candidateCount > 0 && distance (estimate.lagFrames, candidate) <= std::floor (rate * offsetAgreementSeconds);
        candidateCount = agrees ? 2 : 1; // bounded, even during a long uninterrupted playback
        candidate = estimate.lagFrames;
        if (candidateCount < 2) return step;
        step.settled = true;
        step.lagFrames = candidate;
        step.sampleRate = estimate.sampleRate;
        if (! hasBaseline) { hasBaseline = true; baseline = candidate; }
        else if (distance (candidate, baseline) >= std::ceil (rate * offsetJumpSeconds) && ! alreadyHeld)
            step.jumped = true; // retain the original run baseline while held
        step.recovered = alreadyHeld && hasBaseline
            && distance (candidate, baseline) <= std::floor (rate * offsetAgreementSeconds);
        return step;
    }
private:
    bool hasBaseline = false;
    int candidateCount = 0;
    double rate = 0.0;
    std::int64_t baseline = 0, candidate = 0;
};
}
