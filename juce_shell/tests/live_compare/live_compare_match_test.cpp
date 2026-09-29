#include "../../src/live_compare/LiveCompareMatch.h"
#include "../../src/live_compare/LiveCompareOffset.h"
#include "live_compare_offset_test.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <limits>
#include <vector>

using namespace hypha::live_compare;

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "FAIL: " << message << '\n'; std::abort(); }
}

namespace
{
constexpr int frames = 512;
constexpr std::uint32_t rate = 48000;
constexpr std::uint64_t key = 0xBADC0FFEEull;

// Deterministic broadband signal around -20 dBFS; spikeAt places one PRE-only near-full-scale peak.
float noise (std::int64_t index, int channel)
{
    auto z = static_cast<std::uint64_t> (index * 2 + channel) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (static_cast<float> (z >> 40) / 16777216.0f - 0.5f) * 0.2f;
}

// An adjacent pair (K = 0): POST hears PRE scaled by postGain, without PRE's spike when spikeAt >= 0.
struct Pair
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    PreFeeder feeder;
    PostRenderer renderer;
    PostLevel level;
    std::int64_t clock = 0;

    Pair()
    {
        ring->initialise (key, rate);
        ring->header.demand.store (1);
        renderer.prepare (4096, rate);
    }

    void run (double seconds, float preScale, float postGain, std::int64_t spikeAt = -1,
              std::int64_t contentDelay = 0)
    {
        std::vector<float> pre[2], post[2];
        for (int c = 0; c < 2; ++c) { pre[c].assign (frames, 0.0f); post[c].assign (frames, 0.0f); }
        const auto blocks = static_cast<int> (seconds * rate / frames);
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < frames; ++i)
                {
                    const auto t = clock + i;
                    const float base = noise (t, c) * preScale;
                    pre[c][size_t (i)] = t == spikeAt ? 0.95f : base;
                    post[c][size_t (i)] = noise (t - contentDelay, c) * postGain;
                }
            BlockClock block;
            block.clock = block.project = clock;
            block.frames = frames;
            block.clockValid = block.projectValid = block.playing = true;
            const float* in[] = { pre[0].data(), pre[1].data() };
            feeder.feed (*ring, block, in, 2);
            float* io[] = { post[0].data(), post[1].data() };
            renderer.render (*ring, key, rate, block, io, 2, false, 1.0f, level, 1.0f, 1.0f);
            clock += frames;
        }
    }
};
}

// The measured difference is POST minus PRE loudness over the aligned window, within 0.1 dB.
static void matchesALevelDifference()
{
    Pair pair;
    pair.run (5.0, 1.0f, 0.5f);
    const auto r = computeMatch (*pair.ring, pair.renderer, rate);
    std::printf ("level difference: measured %.3f dB over %.2f s (%llu units)\n",
                 r.measuredDb, r.seconds, static_cast<unsigned long long> (r.analysisUnits));
    require (r.ok(), "a proven window with signal is measured");
    require (std::fabs (r.measuredDb - 20.0 * std::log10 (0.5)) < 0.1, "the difference is within 0.1 dB");
    require (r.seconds >= 3.9 && r.seconds <= 4.0, "the window is the latest four seconds");
    const auto plan = planMatch (r, 0.0);
    require (! plan.needsApproval && plan.preGainDb == r.measuredDb && plan.postGainDb == 0.0,
             "a cut needs no approval and never moves POST");
}

// A PRE boost that would push PRE's true peak above the ceiling asks the user: lower POST by the
// whole difference with PRE at its level, or raise PRE only up to the ceiling. Nothing is clamped
// without that choice.
static void aBoostAboveTheCeilingAsksToLowerPost()
{
    Pair pair;
    pair.run (5.0, 0.25f, 1.0f, 48000 * 3); // PRE 12 dB quieter, with one PRE-only peak near full scale
    const auto r = computeMatch (*pair.ring, pair.renderer, rate);
    const auto plan = planMatch (r, 0.0);
    std::printf ("true-peak ceiling: measured %.3f dB, ceiling %.3f dBTP, PRE peak %.3f dBTP\n",
                 r.measuredDb, r.ceilingDbtp, r.prePeakDbtp);
    require (r.ok() && r.measuredDb > 11.0, "the boost is measured");
    require (plan.needsApproval && std::fabs (plan.lowerPostGainDb + r.measuredDb) < 1.0e-9,
             "approved, POST drops by the whole difference and PRE stays at its level");
    require (plan.limitedPreGainDb >= 0.0 && plan.limitedPreGainDb < r.measuredDb
                 && std::fabs (plan.limitedPreGainDb - (r.ceilingDbtp - r.prePeakDbtp)) < 1.0e-9,
             "declined, PRE rises only up to the ceiling");
}

// A POST attenuation the user already approved carries into the next MATCH, which never raises POST.
static void aHeldAttenuationCarriesIntoTheNextMatch()
{
    MatchResult r;
    r.failure = MatchFailure::none;
    r.measuredDb = 7.0;
    r.prePeakDbtp = -3.0;
    r.postPeakDbtp = -1.0;
    r.ceilingDbtp = -1.0;
    const auto same = planMatch (r, -7.0);
    require (! same.needsApproval && std::fabs (same.preGainDb) < 1.0e-12 && same.postGainDb == -7.0,
             "the held attenuation already matches");
    const auto partial = planMatch (r, -3.0);
    require (partial.needsApproval && std::fabs (partial.lowerPostGainDb + 7.0) < 1.0e-12
                 && std::fabs (partial.limitedPreGainDb - 2.0) < 1.0e-12 && partial.postGainDb == -3.0,
             "more lowering needs approval again, declined keeps the held POST");
    r.measuredDb = -5.0;
    const auto louder = planMatch (r, -7.0);
    require (! louder.needsApproval && std::fabs (louder.preGainDb + 12.0) < 1.0e-12 && louder.postGainDb == -7.0,
             "a louder PRE is cut, and POST is not raised");
}

static void validatesFinalGainsBeforeAdmission()
{
    MatchResult measured;
    measured.failure = MatchFailure::none;
    measured.measuredDb = -6.021;
    measured.prePeakDbtp = -6.0;
    measured.ceilingDbtp = -1.0;
    const auto outside = planMatch (measured, -20.0);
    require (outside.failure == MatchFailure::outOfRange, "held -20 plus measured -6.021 is not a valid plan");
    require (validateMatchPlan (outside, MatchChoice::basis) == MatchFailure::outOfRange,
             "processor and preparation use the same failure reason");
    measured.measuredDb = -4.0;
    const auto boundary = planMatch (measured, -20.0);
    require (boundary.failure == MatchFailure::none && boundary.preGainDb == -24.0,
             "exact -24 dB remains accepted");
    auto invalid = boundary;
    invalid.preGainDb = std::numeric_limits<double>::quiet_NaN();
    require (validateMatchPlan (invalid, MatchChoice::basis) == MatchFailure::invalidPlan, "NaN fails closed");
    require (validateMatchPlan (boundary, MatchChoice::lowerPost) == MatchFailure::invalidPlan,
             "a plan cannot be applied through the wrong approval choice");
    require (planMatch (measured, std::numeric_limits<double>::quiet_NaN()).failure == MatchFailure::invalidPlan,
             "NaN held level cannot be normalised to unity");
    measured.failure = MatchFailure::notEnoughSignal;
    require (planMatch (measured, 0.0).failure == MatchFailure::notEnoughSignal, "analysis failure survives planning");
}

// INV-LC7: through the ring and the proven K, a POST that carries PRE 10 ms later than the clocks
// say (a plug-in between that does not report its latency) shows PRE playing 480 frames early.
static void theMappingShowsAnUnreportedDelay()
{
    Pair aligned;
    aligned.run (3.0, 1.0f, 0.5f);
    const auto zero = measureOffset (*aligned.ring, aligned.renderer);
    require (zero.determined && zero.lagFrames == 0, "an aligned chain shows no offset");
    Pair delayed;
    delayed.run (3.0, 1.0f, 0.5f, -1, 480);
    const auto early = measureOffset (*delayed.ring, delayed.renderer);
    std::printf ("mapping offset: lag %lld (peak %.3f)\n", static_cast<long long> (early.lagFrames), early.peak);
    require (early.determined && early.lagFrames == -480, "an unreported 10 ms delay shows PRE 480 frames early");
    require (early.sampleRate == static_cast<double> (rate), "the lag counts in the ring's stamped rate");
    Pair early1;
    early1.run (0.5, 1.0f, 0.5f);
    require (! measureOffset (*early1.ring, early1.renderer).determined, "without enough history nothing is claimed");
}

// INV-LC7 / LC10: one estimate never decides; two agreeing ones settle the offset and the run's
// baseline, and two agreeing ones away from it are a jump, once, while not already held.
static void aJumpNeedsTwoAgreeingEstimates()
{
    const auto at = [] (std::int64_t lag) { OffsetEstimate e; e.determined = true; e.lagFrames = lag; return e; };
    OffsetMonitor monitor;
    require (! monitor.observe (at (0), false).settled, "one estimate settles nothing");
    require (! monitor.observe (OffsetEstimate {}, false).settled, "an undetermined estimate settles nothing");
    const auto base = monitor.observe (at (1), false);
    require (base.settled && base.lagFrames == 1 && ! base.jumped, "two agreeing estimates set the baseline");
    require (! monitor.observe (at (480), false).jumped, "a single outlier is not a jump");
    const auto jump = monitor.observe (at (481), false);
    require (jump.settled && jump.jumped && jump.lagFrames == 481, "two agreeing estimates away from it are a jump");
    require (! monitor.observe (at (481), false).jumped, "the jump becomes the new baseline");
    monitor.observe (at (0), true);
    require (! monitor.observe (at (0), true).jumped, "while held, nothing jumps again");
}

// INV-LC16: AUTO moves PRE only beyond 0.5 dB, never past the ceiling the MATCH approved (a louder
// window's own ceiling does not raise it), never more than 6 dB from the MATCH, keeps POST's
// approved attenuation, and changes nothing when a window does not measure.
static void autoFollowsWithinReachAndStopsAtTheCeiling()
{
    const auto measured = [] (double db, double prePeak, double ceiling)
    {
        MatchResult r;
        r.failure = MatchFailure::none;
        r.measuredDb = db;
        r.prePeakDbtp = prePeak;
        r.ceilingDbtp = ceiling;
        return r;
    };
    const auto step = [] (const MatchResult& r, double held, double current)
    { return followStep (r, held, -6.0, -1.0, current); };
    require (step (measured (-6.4, -10.0, -1.0), 0.0, -6.0).action == FollowAction::keep, "0.4 dB off stays");
    const auto moved = step (measured (-6.6, -10.0, -1.0), 0.0, -6.0);
    require (moved.action == FollowAction::move && std::fabs (moved.preGainDb + 6.6) < 1.0e-9, "0.6 dB off moves PRE to the match");
    const auto held = step (measured (-6.0, -10.0, -1.0), -3.0, -6.0);
    require (held.action == FollowAction::move && std::fabs (held.preGainDb + 9.0) < 1.0e-9,
             "PRE takes what remains with POST at its approved attenuation");
    require (step (measured (-12.5, -10.0, -1.0), 0.0, -6.0).action == FollowAction::stopReach, "6.5 dB from MATCH stops");
    require (step (measured (-0.5, -0.3, 2.0), 0.0, -6.0).action == FollowAction::stopCeiling,
             "a louder window's ceiling never raises the approved one");
    MatchResult quiet;
    quiet.failure = MatchFailure::notEnoughSignal;
    require (step (quiet, 0.0, -6.0).action == FollowAction::keep, "silence keeps the gain");
}

// No proven K, or less than three seconds of contiguous history, measures nothing.
static void unprovenOrShortWindowsAreRefused()
{
    Pair early;
    early.run (0.05, 1.0f, 0.5f);
    require (computeMatch (*early.ring, early.renderer, rate).failure == MatchFailure::notProven,
             "before calibration nothing is measured");
    Pair shortRun;
    shortRun.run (2.0, 1.0f, 0.5f);
    require (computeMatch (*shortRun.ring, shortRun.renderer, rate).failure == MatchFailure::tooShort,
             "two seconds are too short");
    // The 2MIX policy itself refuses PRE more than 24 dB quieter; PRE 30 dB louder reaches the gate.
    Pair far;
    far.run (5.0, 1.0f, 0.03f);
    const auto farResult = computeMatch (*far.ring, far.renderer, rate);
    std::printf ("out of range: failure %d, measured %.3f dB\n", static_cast<int> (farResult.failure),
                 farResult.measuredDb);
    require (farResult.failure == MatchFailure::outOfRange, "a difference beyond 24 dB is refused");
}

int main()
{
    matchesALevelDifference();
    aBoostAboveTheCeilingAsksToLowerPost();
    aHeldAttenuationCarriesIntoTheNextMatch();
    validatesFinalGainsBeforeAdmission();
    unprovenOrShortWindowsAreRefused();
    theMappingShowsAnUnreportedDelay();
    autoFollowsWithinReachAndStopsAtTheCeiling();
    aJumpNeedsTwoAgreeingEstimates();
    hypha::tests::verifyLiveCompareOffset();
    std::printf ("live compare match: all checks passed\n");
    return 0;
}
