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
            renderer.render (*ring, key, rate, block, io, 2, false, 1.0f);
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
    std::printf ("level difference: measured %.3f dB, applied %.3f dB over %.2f s (%llu units)\n",
                 r.measuredDb, r.appliedDb, r.seconds, static_cast<unsigned long long> (r.analysisUnits));
    require (r.ok(), "a proven window with signal is measured");
    require (std::fabs (r.measuredDb - 20.0 * std::log10 (0.5)) < 0.1, "the difference is within 0.1 dB");
    require (! r.limitedByTruePeak && r.appliedDb == r.measuredDb, "a cut is never limited");
    require (r.seconds >= 3.9 && r.seconds <= 4.0, "the window is the latest four seconds");
}

// A PRE boost that would push PRE's true peak above the ceiling is limited, and reported.
static void limitsABoostAtTheTruePeakCeiling()
{
    Pair pair;
    pair.run (5.0, 0.25f, 1.0f, 48000 * 3); // PRE 12 dB quieter, with one PRE-only peak near full scale
    const auto r = computeMatch (*pair.ring, pair.renderer, rate);
    std::printf ("true-peak limit: measured %.3f dB, applied %.3f dB, ceiling %.3f dBTP\n",
                 r.measuredDb, r.appliedDb, r.ceilingDbtp);
    require (r.ok() && r.measuredDb > 11.0, "the boost is measured");
    require (r.limitedByTruePeak && r.appliedDb < r.measuredDb, "the boost is limited at the ceiling");
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
}

int main()
{
    matchesALevelDifference();
    limitsABoostAtTheTruePeakCeiling();
    unprovenOrShortWindowsAreRefused();
    std::printf ("live compare match: all checks passed\n");
    return 0;
}
