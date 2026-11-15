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
// plug-in between under-reports its latency. Undetermined when the window is too quiet or
// heavily processed. Repeated scaled copies require block checks, not a unique-position claim.
struct OffsetWindow
{
    std::int64_t end = 0;
    std::uint64_t proof = 0, preRun = 0;
    std::int64_t center = 0; // where the fine search was centred: another center is another window
    bool operator== (const OffsetWindow& other) const noexcept
    { return end == other.end && proof == other.proof && preRun == other.preRun && center == other.center; }
};

#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
inline void (*beforeOffsetWindowForTest)() = nullptr;
inline void (*beforeContentEstimateForTest)() = nullptr;
inline bool (*keptViewMissForTest)() = nullptr; // true: the kept correction's reach is not known yet
#endif

struct OffsetEstimate
{
    bool determined = false;
    bool verifySamples = false; // repeated full-window copies; the renderer must check every block
    std::int64_t lagFrames = 0;
    double peak = 0.0;      // GCC-PHAT peak or full-window scaled sample agreement
    double dominance = 0.0; // spectral peak ratio; infinite for a unique full-window sample match
    std::int64_t clockK = 0;
    std::int32_t blockFrames = 0;
    std::uint64_t stopGeneration = 0, timelineGeneration = 0;
    AlignContext alignContext;
    std::uint64_t alignRevision = 0;
    OffsetWindow window; // identity of the actual copied history, never a separate pre-copy sample
    bool windowReady = false; // copies validated, even if the content has no distinct peak
    bool narrow = false; // narrow (strong bass), no scaled copy, no strong peak: no evidence, no doubt
    double sampleRate = 0.0; // the rate lagFrames counts in: the rate the ring is stamped with
};

constexpr std::int64_t offsetSearchFrames = 8192;  // +/-170 ms at 48 kHz
constexpr std::int64_t offsetWindowFrames = 32768; // 0.68 s at 48 kHz

// Non-RT. post holds `frames` mono samples; pre holds frames + 2 * maxLag, its index 0 aligned with
// POST index -maxLag. Cross-correlates with the phase transform (GCC-PHAT), so an EQ or a level
// change between does not move the peak, and polarity does not matter.
OffsetEstimate estimateOffset (const std::vector<float>& post, const std::vector<float>& pre, std::int64_t maxLag);

// Non-RT (message thread): the latest window of POST's input history against PRE's ring through the
// proven K, keeping maxLag of PRE after the window so that PRE has already written it. `center`
// moves the +/-offsetSearchFrames search to an offset a wide search found (lagFrames includes it).
OffsetEstimate measureOffset (const Ring&, const PostRenderer&, std::int64_t center = 0);

// A chain delay the host does not compensate can exceed the fine search: a linear-phase EQ at high
// oversampling holds tens of thousands of samples. The wide search correlates POST with PRE under the
// mapping hypotheses in order (straight, exchanged, each POST channel against PRE's sum), over up to the
// last offsetWideHistorySeconds of POST's history and PRE's run at about 3 kHz (whitened, as above), and
// reaches as far as both sides overlap by offsetWideOverlapSeconds. The first hypothesis in which a
// channel's own pair has a dominant peak decides; two such channels at different positions propose
// nothing. A channel without one only waits for the full reach. It only proposes a center: the fine
// search there, with its own peak, agreement and dominance rules, is the evidence.
constexpr double offsetWideHistorySeconds = 8.0, offsetWideOverlapSeconds = 1.0, offsetWideDominance = 3.0;
inline std::int64_t offsetWideFrames (double rate) noexcept
{ return rate > 0.0 && rate <= 768000.0 ? static_cast<std::int64_t> (rate * offsetWideHistorySeconds) : 0; }
struct WideOffset
{
    bool found = false;
    std::int64_t lagFrames = 0; // the same sign as OffsetEstimate: positive when PRE plays late
    double peak = 0.0, dominance = 0.0;
};
// recentFrames > 0 looks only that far back: after a delay was added while playing, the older history
// still shows the old position and outweighs the new one for seconds. A shorter span reaches less far.
WideOffset searchWideOffset (const Ring&, const PostRenderer&, std::int64_t recentFrames = 0);

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
        bool verifySamples = false;
    };
    Step observe (const OffsetEstimate& estimate, bool alreadyHeld) noexcept
    {
        Step step;
        // A narrow window is not judged at all: it neither votes nor breaks consecutive votes.
        if (! offsetCanVote (estimate)) { if (! estimate.narrow) candidateCount = 0; return step; }
        if (std::abs (rate - estimate.sampleRate) > 0.0) { *this = {}; rate = estimate.sampleRate; }
        const auto distance = [] (std::int64_t a, std::int64_t b)
        { return std::abs (static_cast<long double> (a) - static_cast<long double> (b)); };
        const bool agrees = candidateCount > 0 && estimate.verifySamples == candidateVerifySamples
            && distance (estimate.lagFrames, candidate) <= std::floor (rate * offsetAgreementSeconds);
        candidateCount = agrees ? 2 : 1; // bounded, even during a long uninterrupted playback
        candidate = estimate.lagFrames;
        candidateVerifySamples = estimate.verifySamples;
        if (candidateCount < 2) return step;
        step.settled = true;
        step.lagFrames = candidate;
        step.sampleRate = estimate.sampleRate;
        step.verifySamples = estimate.verifySamples;
        if (step.verifySamples) return step; // no unique position: never establish a jump baseline
        if (! hasBaseline) { hasBaseline = true; baseline = candidate; }
        else if (distance (candidate, baseline) >= std::ceil (rate * offsetJumpSeconds) && ! alreadyHeld)
            step.jumped = true; // retain the original run baseline while held
        step.recovered = alreadyHeld && hasBaseline
            && distance (candidate, baseline) <= std::floor (rate * offsetAgreementSeconds);
        return step;
    }
private:
    bool hasBaseline = false;
    bool candidateVerifySamples = false;
    int candidateCount = 0;
    double rate = 0.0;
    std::int64_t baseline = 0, candidate = 0;
};
}
