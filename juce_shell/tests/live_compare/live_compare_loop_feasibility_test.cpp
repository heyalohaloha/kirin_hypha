#include "LiveCompareLoopOracle.h"
#include "LoopEntryEvidenceContract.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace loop_feasibility;

static void require (bool ok, const char* message)
{
    if (! ok) { std::fprintf (stderr, "FAIL: %s\n", message); std::abort(); }
}

static void print (const char* name, const Result& r)
{
    std::printf ("%s: blocks=%llu accepted=%llu wrong=%llu PRE=%.2f%% firstWrongOffset=%lld\n", name,
        static_cast<unsigned long long> (r.blocks), static_cast<unsigned long long> (r.accepted),
        static_cast<unsigned long long> (r.wrong), 100.0 * static_cast<double> (r.acceptedFrames)
            / static_cast<double> (r.totalFrames), static_cast<long long> (r.firstWrongOffset));
}

constexpr std::array<int, 5> variable { 64, 128, 256, 512, 2048 };
constexpr std::array<int, 5> blockSizes { 64, 128, 256, 512, 0 }; // 0 = variable

static Result run (std::int64_t length, std::int64_t delay, Clock clock, Position position,
                   int block, int laps, bool looping = true)
{
    Oracle host (length, delay, clock, position, false, looping);
    unsigned n = 0;
    while (host.elapsed() < laps * length)
        host.step (block > 0 ? block : variable[n++ % variable.size()]);
    return host.tally();
}

static void oracleDetectsMutations()
{
    constexpr std::int64_t length = 24000;
    std::array<std::array<float, maximumBlock>, 2> expected {};
    const float* received[] { expected[0].data(), expected[1].data() };
    // Independently calculate the delay's expected input sample. Also verify both channels,
    // the last sample, a one-sample error, a whole-lap error, NaN and invalid fixture inputs.
    for (const auto delay : { 0, 4096, 23999, 24000, 24001, static_cast<int> (ringCapacityFrames) + 1 })
    {
        Oracle host (length, delay, Clock::renderCounter, Position::content);
        require (! host.matchesExpected (received), "an empty oracle must never report matching audio");
        for (unsigned n = 0; n < 80; ++n)
        {
            const auto frames = variable[n % variable.size()];
            host.step (frames);
            const auto fill = [&] (std::int64_t shift)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < frames; ++i)
                        expected[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)]
                            = token (host.expectedStart() + i + shift, c);
            };
            fill (0);
            require (host.matchesExpected (received), "delay-line oracle disagrees with emitted-frame identity");
            fill (1);
            require (! host.matchesExpected (received), "oracle missed one sample of misalignment");
            fill (length);
            require (! host.matchesExpected (received), "oracle missed a whole-lap misalignment");
            fill (0);
            expected[1][static_cast<std::size_t> (frames - 1)] += 0.25f;
            require (! host.matchesExpected (received), "oracle missed the final sample on channel 2");
            fill (0);
            expected[0][0] = std::numeric_limits<float>::quiet_NaN();
            require (! host.matchesExpected (received), "oracle accepted nonfinite PCM");
        }
    }
    const auto rejects = [] (auto action)
    {
        try { action(); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    require (rejects ([] { Oracle host (0, 0, Clock::vst3, Position::content); }), "zero-length loop accepted");
    require (rejects ([] { Oracle host (24000, -1, Clock::vst3, Position::content); }), "negative delay accepted");
    require (rejects ([] { Oracle host (24000, 2 * ringCapacityFrames + 1, Clock::vst3, Position::content); }),
             "unbounded fixture allocation accepted");
    Oracle host (24000, 0, Clock::vst3, Position::content);
    require (rejects ([&] { host.step (0); }) && rejects ([&] { host.step (maximumBlock + 1); }),
             "invalid block size accepted");
    std::puts ("oracle self-test: exact delay / one sample / whole lap / last frame / NaN / bounds PASS");
}

// The fixture's independent counter origins do not encode delay. With missing presentation
// information, delays 0 and one lap have identical metadata but different correct audio.
// This demonstrates an input-model ambiguity, not every AU/AAX host's actual behaviour.
static void initialLoopClockAmbiguity()
{
    constexpr int length = 24000, frames = 128;
    Oracle adjacent (length, 0, Clock::renderCounter, Position::content);
    Oracle delayed (length, length, Clock::renderCounter, Position::content);
    Oracle repeatedAdjacent (length, 0, Clock::renderCounter, Position::content, true);
    Oracle repeatedDelayed (length, length, Clock::renderCounter, Position::content, true);
    for (int i = 0; i < 1000; ++i)
    {
        require (sameObservation (adjacent.observations (frames), delayed.observations (frames)),
                 "model's delay ambiguity must preserve the observed clock trace");
        adjacent.step (frames);
        delayed.step (frames);
        repeatedAdjacent.step (frames);
        repeatedDelayed.step (frames);
        require (adjacent.expectedDiffers (delayed, frames), "per-occurrence PCM failed to distinguish laps");
        require (! repeatedAdjacent.expectedDiffers (repeatedDelayed, frames),
                 "identical project PCM should hide the one-lap difference in this control");
    }
    // Product results are observations, not pass conditions requiring a bug to survive.
    print ("raw Consumer, adjacent loop", adjacent.tally());
    print ("raw Consumer, one-lap delay", delayed.tally());
    std::puts ("fixture: 1000 identical metadata blocks / 1000 differing per-occurrence PCM blocks");
}

static void nonLoopPositiveControls()
{
    for (const auto clock : { Clock::vst3, Clock::renderCounter })
    for (const auto delay : { 0, 4096 })
    for (const auto block : blockSizes)
    {
        const auto r = run (24000, delay, clock, Position::content, block, 4, false);
        require (r.wrong == 0 && r.acceptedFrames * 100 >= r.totalFrames * 90,
                 "non-loop positive control must actually read the correct PRE, not only reject");
    }
    std::puts ("non-loop Consumer positive controls: 20 PASS");
}

static void precalibratedContinuation()
{
    constexpr int length = 24000, frames = 128;
    for (const auto clock : { Clock::vst3, Clock::renderCounter })
    for (const int delay : { 4096, length + 1 })
    {
        Oracle host (length, delay, clock, Position::content, false, false);
        Decision before;
        for (int i = 0; i < 16 + (delay + frames - 1) / frames; ++i) before = host.step (frames);
        require (before.kValid && before.verdict == Verdict::accepted && host.tally().wrong == 0,
                 "continuation test needs an already-correct K, not loop acquisition");
        const auto atActivation = host.observations (frames);
        host.enableLoopAtCurrentPosition();
        const auto afterActivation = host.observations (frames);
        require (sameSampleClock (atActivation.pre, afterActivation.pre)
                     && sameSampleClock (atActivation.post, afterActivation.post), "enabling loop must not seek");
        require (! atActivation.pre.loop.active && afterActivation.pre.loop.active
                     && ! atActivation.post.loop.active && afterActivation.post.loop.active
                     && ! sameObservation (atActivation, afterActivation),
                 "the full observation must still distinguish the loop activation");
        Decision after;
        for (int i = 0; i < (4 * length + frames - 1) / frames; ++i) after = host.step (frames);
        char name[128];
        std::snprintf (name, sizeof (name), "precalibrated K, clock=%s delay=%d K=%lld->%lld",
            clock == Clock::vst3 ? "VST3-like" : "counter", delay,
            static_cast<long long> (before.k), static_cast<long long> (after.k));
        print (name, host.tally());
        // Observe current Consumer behaviour without requiring a bug after a future fix.
        // This does not run MATCH gains, processor discontinuity policy or live Blind.
    }
}

static bool meetsExploratoryThreshold (const Result& r)
{
    return r.totalFrames != 0 && r.wrong == 0 && r.acceptedFrames * 100 >= r.totalFrames * 95;
}

static void surveyPredicateControls()
{
    Result result;
    result.totalFrames = 100; result.acceptedFrames = 100;
    require (meetsExploratoryThreshold (result), "a fully correct result must be allowed to pass");
    result.wrong = 1;
    require (! meetsExploratoryThreshold (result), "one wrong block must fail regardless of coverage");
    result.wrong = 0; result.acceptedFrames = 0;
    require (! meetsExploratoryThreshold (result), "all-POST fallback must not pass");
    result.acceptedFrames = 94;
    require (! meetsExploratoryThreshold (result), "coverage threshold lower boundary");
    result.acceptedFrames = 95;
    require (meetsExploratoryThreshold (result), "coverage threshold exact boundary");
    result.totalFrames = 0;
    require (! meetsExploratoryThreshold (result), "empty evidence cannot pass");
}

// Raw Consumer survey only. It deliberately does NOT drive the processor's discontinuity,
// MATCH, Blind or gain safety policies. Neither a passing nor failing survey certifies a host.
static unsigned survey()
{
    unsigned unsafe = 0, undercovered = 0, unresolved = 0, cases = 0;
    for (const int length : { 24000, 48000, 96000, 192000, 384000 })
    for (const auto clock : { Clock::vst3, Clock::renderCounter })
    for (const auto position : { Position::content, Position::clamp })
    for (const int delay : { 0, 4096, length - 1, length, length + 1 })
    for (const int block : blockSizes)
    {
        const auto r = run (length, delay, clock, position, block, 100);
        ++cases;
        char name[160];
        std::snprintf (name, sizeof (name), "loop=%d delay=%d clock=%s project=%s block=%d", length, delay,
            clock == Clock::vst3 ? "VST3-like" : "counter", position == Position::content ? "content" : "clamp", block);
        print (name, r);
        if (r.wrong != 0) ++unsafe;
        if (r.acceptedFrames * 100 < r.totalFrames * 95) ++undercovered;
        if (! meetsExploratoryThreshold (r)) ++unresolved;
    }
    require (cases == 500, "survey must cross every delay with every buffer schedule");
    for (const int block : blockSizes)
    {
        const auto r = run (24000, ringCapacityFrames + 1, Clock::renderCounter, Position::content, block, 4);
        char name[80];
        std::snprintf (name, sizeof (name), "beyond ring capacity, block=%d", block);
        print (name, r);
        // Correct behaviour outside capacity is rejection, not >95% PRE coverage.
        if (r.accepted != 0) ++unresolved;
    }
    std::printf ("raw Consumer survey: %u cases x 100 laps; unsafe=%u undercovered=%u (may overlap); "
                 "unresolved including 5 capacity cases=%u\n", cases, unsafe, undercovered, unresolved);
    return unresolved;
}

int main (int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::strcmp (argv[1], "--survey") != 0))
    {
        std::fputs ("Usage: KirinLiveCompareLoopFeasibilityTests [--survey]\n", stderr);
        return 64;
    }
    oracleDetectsMutations();
    initialLoopClockAmbiguity();
    loop_entry_evidence::verify();
    nonLoopPositiveControls();
    precalibratedContinuation();
    surveyPredicateControls();
    std::puts ("fixture controls: PASS (not LOOP product acceptance)");
    if (argc == 1) return 0;
    const auto unresolved = survey();
    std::printf ("raw Consumer exploratory threshold: %s; product/host qualification: NOT RUN\n",
                 unresolved == 0 ? "PASS" : "FAIL");
    return unresolved == 0 ? 0 : 2;
}
