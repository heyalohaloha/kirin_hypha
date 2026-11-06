#pragma once
#include "AaxChunkChain.h"
#include <thread>

// INV-LC28 in a whole chain: PRE 21.33 ms early, as an insert the host does not compensate leaves it.
namespace live_compare_align_test
{
using namespace aax_chunk_test;
constexpr std::int64_t provenK = -512, early = 1024;

inline std::unique_ptr<Chain> sounding (Options options = {})
{
    options.k = provenK;
    auto chain = std::make_unique<Chain> (options);
    chain->run (30);
    return chain;
}
}

static void alignMovesOnlyTheReadAddress()
{
    using namespace live_compare_align_test;
    auto chain = sounding();
    require (chain->postRenderer().alignFrames() == 0 && chain->report.preAudible, "the fixture reads at K alone");
    chain->align (alignValue (-early));
    chain->run (4);
    require (chain->postRenderer().alignFrames() == early, "the value is applied");
    require (chain->decision.kValid && chain->decision.k == provenK, "K stays the clocks' proof");
    require (chain->wrongAddress == 0 && chain->wrongPcm == 0, "PRE is read that much later, and every sample is right");
    require (chain->report.verdict == Verdict::accepted && chain->report.preAudible, "PRE sounds there");
}

// A new address never jumps under PRE: it fades to POST, moves, and fades back.
static void alignFadesInsteadOfJumping()
{
    using namespace live_compare_align_test;
    Options options;
    options.postFrames = 64; // the 240-frame fade spans four callbacks
    auto chain = sounding (options);
    chain->align (alignValue (-early));
    int callback = -1;
    float weight = 1.0f;
    for (int chunk = 0; chunk < 2; ++chunk)
        chain->chunk ([&] (int, BlockClock&) {
            if (callback >= 0)
            {
                const auto alignment = callback < 4 ? 0 : early;
                require (chain->postRenderer().alignFrames() == alignment, "the address waits for the full fade out");
                require (chain->report.verdict == Verdict::accepted, "the proof holds throughout the fades");
                if (callback == 3) require (! chain->postRenderer().hasPre(), "old PRE is silent before the address moves");
                const auto target = callback < 4 ? 0.0f : 1.0f;
                for (int frame = 0; frame < options.postFrames; ++frame)
                {
                    weight = weight < target ? std::min (target, weight + 1.0f / 240.0f)
                                             : std::max (target, weight - 1.0f / 240.0f);
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        const auto pre = pcm (chain->renderedClock - provenK - alignment + frame, channel);
                        const auto wanted = postSample * (1.0f - weight) + pre * weight;
                        require (std::abs (chain->samples()[channel][frame] - wanted) < 1.0e-6f,
                                 "both 5 ms fades use independently timed old and new PRE samples");
                    }
                }
            }
            ++callback;
        });
    require (chain->postRenderer().alignFrames() == early && chain->report.preAudible, "PRE returns on the new address");
    require (chain->wrongPcm == 0, "every settled block is the new address or the old one, never a jump");
}

// A Blind trial gives no clue: the value waits until the listener is on POST.
static void alignWaitsForSilenceInATrial()
{
    using namespace live_compare_align_test;
    auto chain = sounding();
    chain->trial (true);
    chain->align (alignValue (-early));
    chain->run (12);
    require (chain->postRenderer().alignFrames() == 0 && chain->report.preAudible, "PRE keeps its address while it sounds");
    chain->relocate (48000);
    chain->run (30);
    require (chain->postRenderer().alignFrames() == 0 && chain->report.preAudible,
             "a timing interruption is not the listener switching sources");
    chain->select (false);
    chain->run (2);
    require (chain->postRenderer().alignFrames() == early, "the listener's own switch carries the change");
    chain->select (true);
    chain->run (2);
    require (chain->report.preAudible && chain->wrongPcm == 0 && chain->wrongAddress == 0, "PRE returns aligned");
}

// Seek, stop, loop and a recalibrated clock origin keep the same content delay.
static void alignSurvivesClockRecalibration()
{
    using namespace live_compare_align_test;
    auto chain = sounding();
    chain->align (alignValue (-early));
    chain->run (4);
    chain->relocate (48000);
    chain->run (30);
    require (chain->decision.k == provenK && chain->postRenderer().alignFrames() == early, "a seek keeps the value");
    chain->chunk ([] (int i, BlockClock& b) { if (i > 0) b.playing = false; });
    chain->chunk ([] (int, BlockClock& b) { b.playing = false; });
    chain->run (30);
    require (chain->postRenderer().alignFrames() == early && chain->report.preAudible, "a stop keeps the value");
    chain->enterLoop (480000);
    chain->run (30);
    require (chain->postRenderer().alignFrames() == early && chain->report.preAudible, "a loop keeps the value");
    chain->leaveLoop();
    chain->setK (provenK + 1024);
    chain->run (30);
    require (chain->decision.k == provenK + 1024 && chain->postRenderer().alignFrames() == early
             && chain->report.preAudible, "a new clock proof uses the retained content delay");
    require (chain->wrongAddress == 0 && chain->wrongPcm == 0, "nothing wrong sounded on the way");
}

// Only an early PRE can be read later, and only inside the ring.
static void alignRefusesWhatItCannotDo()
{
    using namespace live_compare_align_test;
    require (alignValue (480).frames == 0, "PRE late has no written samples to read");
    require (alignValue (0).frames == 0 && alignValue (-1).frames == 0, "a frame's rounding is no offset");
    require (alignValue (-static_cast<std::int64_t> (ringCapacityFrames)).frames == 0, "past the ring is not aligned");
    std::int64_t start = 0;
    require (! alignedReadStart (INT64_MIN, 1, {}, start)
        && ! alignedReadStart (INT64_MIN, 0, alignValue (-1024), start), "address arithmetic cannot overflow");
    for (const auto k : { std::int64_t (1) << 40, -(std::int64_t (1) << 40), std::int64_t (0) })
        require (alignedReadStart (2048, k, alignValue (-1024), start) && start == 1024 - k,
                 "any proven clock origin uses the retained content delay");
    require (alignedReadStart (2048, 512, {}, start) && start == 1536, "no value reads at K alone");
}

// INV-LC22 at the boundary of the lead: below zero PRE's written samples bound the candidate.
static void alignLoopProofStopsAtWhatPreWrote()
{
    using namespace aax_chunk_test;
    const auto candidate = [] (std::int64_t k, std::int64_t lead, std::int64_t length = 48000)
    {
        auto pre = aaxBlock (50000, 4000, 1024);
        pre.loop = { true, true, 4000.0 / 24000.0, 0.0, static_cast<double> (length) / 24000.0, 120.0 };
        auto post = aaxBlock (pre.clock + pre.frames - 256 - lead, 0);
        post.project = positiveModulo (post.clock - (pre.clock - pre.project + k), length);
        post.loop = pre.loop; post.loop.ppq = static_cast<double> (post.project) / 24000.0;
        LoopAnchor anchor;
        anchor.loopKnown = true; anchor.loopSamples = length; anchor.runStart = 1000;
        anchor.clock = pre.clock - length; anchor.project = pre.project; anchor.loop = pre.loop;
        return initialLoopCandidate (anchor, pre, post, length, rate, ringCapacityFrames);
    };
    const auto within = candidate (-512, 1024);
    require (within.valid && within.k == -512 && within.kind == LoopEntryKind::boundedEngine, "a written address proves K below zero");
    require (! candidate (-1024, 512).valid, "an address past PRE's write end proves nothing");
    require (candidate (2048, 1024).k == 2048, "K above zero keeps its proof");
    const auto compact = candidate (-512, 1024, 17000);
    require (compact.valid && compact.k == -512, "a short loop with exactly one possible address remains usable");
    require (! candidate (-800, 1024, 17000).valid, "K 16200 and -800 cannot be separated by the bound");
    require (! candidate (-512, 18000, 17000).valid, "two written negative candidates cannot choose a lap");

}

static void aaxPendingCycleCannotSurviveASeek()
{
    using namespace aax_chunk_test;
    AaxChunkPosition position;
    std::int64_t clock = 1000;
    const auto chunk = [&] (std::int64_t project)
    {
        BlockClock block;
        for (int i = 0; i < 4; ++i, clock += 256)
        {
            block = aaxBlock (clock, project);
            block.loop = { true, true, static_cast<double> (project) / 24000.0, 0.0, 2.0, 120.0 };
            position.apply (block, rate);
        }
        return block;
    };
    chunk (47500); chunk (524); // first wrap is not yet a chunk proof
    chunk (1549); // one sample beyond the expected step: old evidence ends here
    const auto fresh = chunk (2573);
    require (position.proven() && fresh.chunkCycleSamples == 0,
             "a newly closed chunk cannot reuse a cycle from before a seek");
}

static void liveCompareAlignContract()
{
    // A contended publication retains the last proven context's value, never a torn tuple or
    // an accidental zero. A complete tuple from an old context must still be rejected.
    hypha::live_compare::AlignRequest request;
    const hypha::live_compare::AlignContext context { 8, 12 };
    const hypha::live_compare::AlignValue a { 101 }, b { 202 }, prior { 303 };
    request.publish (a, context);
    std::thread writer ([&] { for (int i = 0; i < 10000; ++i) request.publish (i % 2 ? a : b, context); });
    for (int i = 0; i < 10000; ++i)
    {
        const auto word = request.read (context, prior);
        require (word == a || word == b || word == prior, "publication contention keeps a complete value or the prior value");
    }
    writer.join();
    require (request.read ({ 9, 12 }, prior).frames == 0 && request.read ({ 8, 13 }, prior).frames == 0,
             "neither a pair nor a format change can retain the prior context's value");
    alignMovesOnlyTheReadAddress();
    alignFadesInsteadOfJumping();
    alignWaitsForSilenceInATrial();
    alignSurvivesClockRecalibration();
    alignRefusesWhatItCannotDo();
    alignLoopProofStopsAtWhatPreWrote();
    aaxPendingCycleCannotSurviveASeek();
}
