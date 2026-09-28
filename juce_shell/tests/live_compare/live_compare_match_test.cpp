#include "../../src/live_compare/LiveCompareMatch.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
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

    void run (double seconds, float preScale, float postGain, std::int64_t spikeAt = -1)
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
                    post[c][size_t (i)] = noise (t, c) * postGain;
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
    unprovenOrShortWindowsAreRefused();
    std::printf ("live compare match: all checks passed\n");
    return 0;
}
