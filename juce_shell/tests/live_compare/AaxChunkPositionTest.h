#pragma once
#include "AaxChunkChain.h"

#include <cmath>
#include <cstdio>
#include <limits>

// The whole measured shape, block by block, in the numbers of the trace.
static void aaxChunkIsNamedOnlyAfterItCloses()
{
    using namespace aax_chunk_test;
    AaxChunkPosition position;
    const auto seen = [&position] (std::int64_t clock, std::int64_t project, int frames = 256)
    { auto b = aaxBlock (clock, project, frames); position.apply (b, rate); return b; };
    auto b = seen (1068544, -1252);
    require (b.projectValid && b.project == -1252 && ! b.positionInterpolated, "a first report is the host's own");
    for (int i = 1; i < 4; ++i)
    {
        b = seen (1068544 + i * 256, -1252);
        require (b.projectValid && b.project == -1252 && ! b.positionInterpolated && ! position.proven(),
                 "until a chunk closes the host's numbers pass as they are");
    }
    b = seen (1069568, -228);
    require (b.projectValid && b.project == -228 && ! b.positionInterpolated, "the closing report is the host's own");
    for (int i = 1; i < 4; ++i)
    {
        b = seen (1069568 + i * 256, -228);
        require (b.projectValid && b.project == -228 + i * 256 && b.positionInterpolated,
                 "a proven chunk names the callbacks the host did not report");
    }
    b = seen (1070592, 796);
    require (b.projectValid && b.project == 796 && ! b.positionInterpolated, "each chunk starts with a host report");
    AaxChunkPosition half;
    auto first = aaxBlock (275456, -1430, 512), second = aaxBlock (275968, -1430, 512), third = aaxBlock (276480, -406, 512);
    half.apply (first, rate); half.apply (second, rate); half.apply (third, rate);
    auto fourth = aaxBlock (276992, -406, 512);
    half.apply (fourth, rate);
    require (second.projectValid && second.project == -1430 && third.projectValid && fourth.project == 106,
             "the 512-frame buffer closes and is named by the same rule");
}

static bool aaxChunkProven (hypha::live_compare::AaxChunkPosition& position, std::int64_t& clock, std::int64_t& project)
{
    bool named = false;
    for (int i = 0; i < 12; ++i, clock += 256)
    {
        auto b = aax_chunk_test::aaxBlock (clock, project + (i / 4) * 1024);
        position.apply (b, 48000.0);
        named = b.positionInterpolated;
    }
    project += 3 * 1024;
    return named;
}

// One unproven first chunk that ends on a step the rule must refuse.
static void aaxChunkRefusesEveryOtherShapeBeforeItIsProven()
{
    using namespace aax_chunk_test;
    const auto unproven = [] (std::int64_t step, int lastFrames, std::int64_t clockSlip)
    {
        AaxChunkPosition position;
        auto b = aaxBlock (1000, 0);
        position.apply (b, rate);
        for (int i = 1; i < 4; ++i) { b = aaxBlock (1000 + i * 256, 0); position.apply (b, rate); }
        const auto report = aaxBlock (2024 + clockSlip, step, lastFrames);
        b = report;
        position.apply (b, rate);
        require (b.project == report.project && b.projectValid && ! b.positionInterpolated, "a refused report stays the host's own");
        auto next = aaxBlock (2024 + clockSlip + lastFrames, step, lastFrames);
        position.apply (next, rate);
        require (! next.positionInterpolated, "a refused step proves no chunk");
    };
    unproven (1023, 256, 0);  // one frame short
    unproven (1025, 256, 0);  // one frame long
    unproven (768, 256, 0);   // the frames of three callbacks
    unproven (-4096, 256, 0); // a seek back
    unproven (1024, 256, 1);  // the right step on a moved clock
    unproven (1024, 512, 0);  // the right step at another block length
    // A wrap inside the first chunk: the step is not the frames of the chunk, so nothing is proven.
    AaxChunkPosition wrap;
    const auto looped = [] (std::int64_t clock, std::int64_t project)
    {
        auto b = aaxBlock (clock, project);
        b.loop = { true, true, static_cast<double> (project) / 24000.0, 0.0, 2.0, 120.0 };
        return b;
    };
    auto b = looped (1000, 47500); wrap.apply (b, rate);
    for (int i = 1; i < 4; ++i) { b = looped (1000 + i * 256, 47500); wrap.apply (b, rate); }
    auto after = looped (2024, 524); // 47500 + 1024, one loop of 48000 back
    wrap.apply (after, rate);
    require (after.projectValid && after.project == 524, "the wrap is the host's own number");
    auto next = looped (2280, 524);
    wrap.apply (next, rate);
    require (! next.positionInterpolated, "a wrap in the first chunk proves no chunk");
}

// Past a wrap a proven chunk has no sample position until the host's next report measures the
// cycle. PPQ alone never becomes a sample length (INV-LC22).
static void aaxChunkMeasuresTheCycleBeforeNamingPastAWrap()
{
    using namespace aax_chunk_test;
    constexpr std::int64_t length = 48000;
    AaxChunkPosition position;
    std::int64_t clock = 1000;
    const auto seen = [&] (std::int64_t project)
    {
        auto b = aaxBlock (clock, project);
        b.loop = { true, true, static_cast<double> (project) / 24000.0, 0.0, 2.0, 120.0 };
        clock += 256;
        position.apply (b, rate);
        return b;
    };
    for (const std::int64_t head : { 45252, 46276 })
        for (int i = 0; i < 4; ++i) seen (head);
    // The first wrap: the last callback of this chunk lies past the loop end.
    for (int i = 0; i < 3; ++i)
    {
        const auto b = seen (47300);
        require (b.projectValid && b.project == 47300 + i * 256 && (i == 0 || b.positionInterpolated), "before the wrap the chunk is named");
    }
    auto past = seen (47300);
    require (! past.projectValid && ! past.positionInterpolated, "past an unmeasured wrap the position is not named");
    // The host's report after that chunk measures the cycle; the proof goes on.
    auto report = seen (324);
    require (report.projectValid && report.project == 324 && ! report.positionInterpolated, "the wrap is reported by the host");
    auto named = seen (324);
    require (named.positionInterpolated && named.project == 580, "a measured wrap keeps the proven chunk");
    for (int i = 2; i < 4; ++i) seen (324);
    std::int64_t head = 1348;
    for (; head + 768 < length; head += 1024)
        for (int i = 0; i < 4; ++i) seen (head);
    // The next wrap uses that measurement, in samples and in PPQ.
    for (int i = 0; i < 3; ++i) seen (head);
    past = seen (head);
    require (past.positionInterpolated && past.project == head + 768 - length, "the measured cycle names the next wrap");
    require (std::abs (past.loop.ppq - static_cast<double> (past.project) / 24000.0) < 1.0e-9, "PPQ follows the same wrap");
    // A wrap of another length is not this loop: the proof ends and the numbers stay the host's.
    const auto wrong = head + 1024 - length + 1;
    report = seen (wrong);
    require (report.project == wrong && ! report.positionInterpolated, "another cycle length is refused");
    named = seen (wrong);
    require (! named.positionInterpolated && named.projectValid && named.project == wrong, "the proof does not survive it");
}

// A measured cycle belongs to one loop, and only a crossed loop end measures one.
static void aaxChunkKeepsTheCycleToItsLoop()
{
    using namespace aax_chunk_test;
    struct Sequence
    {
        AaxChunkPosition position;
        std::int64_t clock = 1000;
        LoopContext loop { true, true, 0.0, 0.0, 2.0, 120.0 };
        BlockClock seen (std::int64_t project)
        {
            auto b = aaxBlock (clock, project);
            b.loop = loop;
            b.loop.ppq = static_cast<double> (project) / 24000.0;
            clock += 256;
            position.apply (b, rate);
            return b;
        }
        void chunk (std::int64_t head) { for (int i = 0; i < 4; ++i) seen (head); }
    };
    const auto measured = [] (Sequence& s)
    {
        for (const std::int64_t head : { 45252, 46276, 47300 }) s.chunk (head);
        const auto report = s.seen (324);
        require (report.chunkCycleSamples == 48000, "the wrap measures the cycle");
        for (int i = 1; i < 4; ++i) s.seen (324);
    };
    {
        Sequence s;
        measured (s);
        s.loop.active = false; // Loop Playback off at a chunk boundary, the step exact
        const auto off = s.seen (1348);
        require (off.chunkCycleSamples == 0 && s.position.proven(), "leaving the loop discards its cycle, not the chunk");
        for (int i = 1; i < 4; ++i) s.seen (1348);
        s.loop.active = true; s.loop.end = 2.5; // another range: 60000 samples
        const auto on = s.seen (2372);
        require (on.chunkCycleSamples == 0, "a loop entered again has no cycle");
        for (int i = 1; i < 4; ++i) s.seen (2372);
        std::int64_t head = 3396;
        for (; head + 768 < 60000; head += 1024) s.chunk (head);
        for (int i = 0; i < 3; ++i) s.seen (head);
        const auto past = s.seen (head);
        require (! past.projectValid && ! past.positionInterpolated && past.chunkCycleSamples == 0,
                 "the old cycle never names a wrap of the new loop");
        const auto next = s.seen (head + 1024 - 60000);
        require (next.chunkCycleSamples == 60000, "the new loop measures its own");
    }
    {
        Sequence s; // the range or tempo moves while the loop stays on
        measured (s);
        s.loop.end = 2.5;
        require (s.seen (1348).chunkCycleSamples == 0 && ! s.position.proven(), "a new range discards the cycle and the proof");
        Sequence t;
        measured (t);
        t.loop.bpm = 130.0;
        require (t.seen (1348).chunkCycleSamples == 0 && ! t.position.proven(), "a new tempo discards the cycle and the proof");
    }
    // Inside a lap a step that is one sample off is no wrap, however close its PPQ rounds.
    for (const std::int64_t step : { 1023, 1025 })
    {
        AaxChunkPosition position;
        std::int64_t clock = 1000;
        const auto seen = [&] (std::int64_t project, double ppq)
        {
            auto b = aaxBlock (clock, project);
            b.loop = { true, true, ppq, 0.0, 2.0, 120.0 };
            clock += 256;
            position.apply (b, rate);
            return b;
        };
        for (const std::int64_t head : { 1024, 2048, 3072 })
            for (int i = 0; i < 4; ++i) seen (head, static_cast<double> (head) / 24000.0);
        require (position.proven(), "the fixture proves a chunk");
        const auto b = seen (3072 + step, 4096.0 / 24000.0);
        require (b.chunkCycleSamples == 0 && ! position.proven() && b.project == 3072 + step && b.projectValid,
                 "a step one sample off measures no cycle and keeps no proof");
    }
}

// One report names one PPQ. A repeated position whose PPQ moved or is missing is not that shape.
static void aaxChunkRefusesAContradictoryPpq()
{
    using namespace aax_chunk_test;
    const auto beat = [] (std::int64_t project) { return static_cast<double> (project) / 24000.0; };
    const auto looped = [] (std::int64_t clock, std::int64_t project, double ppq)
    {
        auto b = aaxBlock (clock, project);
        b.loop = { true, true, ppq, 0.0, 2.0, 120.0 };
        return b;
    };
    const auto insideProvenChunk = [&] (double ppq, bool refused, const char* why)
    {
        AaxChunkPosition position;
        std::int64_t clock = 1000;
        const auto seen = [&] (std::int64_t project, double reported)
        {
            auto b = looped (clock, project, reported);
            clock += 256;
            position.apply (b, rate);
            return b;
        };
        for (const std::int64_t head : { 1024, 2048 })
            for (int i = 0; i < 4; ++i) seen (head, beat (head));
        seen (3072, beat (3072));
        const auto b = seen (3072, ppq);
        require (b.projectValid != refused && b.positionInterpolated != refused && position.proven() != refused, why);
        if (refused) require (b.project == 3072, "a refused block keeps the host's numbers");
    };
    insideProvenChunk (beat (3072) + 0.25, true, "a PPQ that moved while the position stood still is refused");
    insideProvenChunk (beat (3072) - 0.25, true, "a PPQ that moved back is refused");
    insideProvenChunk (std::numeric_limits<double>::quiet_NaN(), true, "a missing PPQ is refused");
    insideProvenChunk (std::numeric_limits<double>::infinity(), true, "an infinite PPQ is refused");
    insideProvenChunk (beat (3072), false, "the same PPQ is the same report");
    insideProvenChunk (beat (3072) + 0.5 / 24000.0, false, "half a sample of rounding is no contradiction");
    // The same before any proof, and in the report that would close a chunk.
    {
        AaxChunkPosition position;
        auto first = looped (1000, 1024, beat (1024)); position.apply (first, rate);
        auto b = looped (1256, 1024, std::numeric_limits<double>::quiet_NaN()); position.apply (b, rate);
        require (b.projectValid && std::isnan (b.loop.ppq) && ! position.proven(), "no proof, no change: the ordinary rules refuse it");
    }
    {
        AaxChunkPosition position;
        for (int i = 0; i < 4; ++i) { auto b = looped (1000 + i * 256, 1024, beat (1024)); position.apply (b, rate); }
        auto report = looped (2024, 2048, beat (2048) + 0.25); position.apply (report, rate);
        auto next = looped (2280, 2048, report.loop.ppq); position.apply (next, rate);
        require (report.project == 2048 && ! position.proven() && ! next.positionInterpolated,
                 "an exact step with a contradictory PPQ proves no chunk");
    }
}

// A chunk belongs to one engine: rate, certificate and format each start it again.
static void aaxChunkProofEndsWithItsEngine()
{
    using namespace aax_chunk_test;
    const auto ends = [] (const char* why, auto change)
    {
        AaxChunkPosition position;
        std::int64_t clock = 1000, project = 0;
        require (aaxChunkProven (position, clock, project), "the fixture proves a chunk");
        auto head = aaxBlock (clock, project); position.apply (head, rate);
        auto sub = aaxBlock (clock + 256, project);
        double next = rate;
        change (sub, next, position);
        const auto sent = sub;
        position.apply (sub, next);
        require (! sub.positionInterpolated && ! position.proven() && sub.project == sent.project, why);
        // Only a chunk the host reports again proves it.
        for (int i = 2; i < 4; ++i) { auto b = aaxBlock (clock + i * 256, project); b.maximumDelaySamples = sent.maximumDelaySamples; position.apply (b, next); require (! b.positionInterpolated, why); }
        auto report = aaxBlock (clock + 1024, project + 1024); report.maximumDelaySamples = sent.maximumDelaySamples;
        position.apply (report, next);
        auto named = aaxBlock (clock + 1280, project + 1024); named.maximumDelaySamples = sent.maximumDelaySamples;
        position.apply (named, next);
        require (! named.positionInterpolated, "one repeat is not a closed chunk");
    };
    ends ("a new sample rate ends the proof", [] (BlockClock&, double& next, AaxChunkPosition&) { next = 96000.0; });
    ends ("a new delay bound ends the proof", [] (BlockClock& b, double&, AaxChunkPosition&) { b.maximumDelaySamples = 32767; });
    ends ("a prepare resets the proof from outside", [] (BlockClock&, double&, AaxChunkPosition& position) { position.reset(); });
    ends ("a new presentation report ends the proof", [] (BlockClock& b, double&, AaxChunkPosition&) { b.outputPresentationValid = true; b.outputPresentationSamples = 64; });
}

// Extreme host values end the proof; none of them may overflow on the way to that decision.
static void aaxChunkArithmeticNeverOverflows()
{
    using namespace aax_chunk_test;
    constexpr auto lowest = std::numeric_limits<std::int64_t>::min();
    constexpr auto highest = std::numeric_limits<std::int64_t>::max();
    for (const auto report : { lowest, highest })
        for (const std::int64_t first : { std::int64_t (0), lowest, highest })
        {
            AaxChunkPosition position;
            for (int i = 0; i < 4; ++i) { auto b = aaxBlock (1000 + i * 256, first); position.apply (b, rate); }
            auto b = aaxBlock (2024, report);
            position.apply (b, rate);
            require (b.project == report && ! position.proven(), "an unrepresentable step proves nothing");
        }
    // A proven chunk at the top of the range: the position past it does not exist.
    {
        AaxChunkPosition position;
        const auto head = highest - 2048 - 512;
        for (const std::int64_t h : { head, head + 1024 })
            for (int i = 0; i < 4; ++i) { auto b = aaxBlock (1000 + (h - head) + i * 256, h); position.apply (b, rate); }
        auto last = aaxBlock (3048, head + 2048); position.apply (last, rate);
        auto a = aaxBlock (3304, head + 2048); position.apply (a, rate);
        auto b = aaxBlock (3560, head + 2048); position.apply (b, rate);
        require (a.positionInterpolated && b.positionInterpolated, "the range still holds these positions");
        auto c = aaxBlock (3816, head + 2048); position.apply (c, rate);
        require (! c.projectValid && ! c.positionInterpolated && ! position.proven(), "a position past the range ends the proof");
    }
    // A named wrap below the range. The first wrap lands exactly on the lowest position.
    {
        AaxChunkPosition position;
        std::int64_t clock = 1000;
        const auto seen = [&] (std::int64_t real)
        {
            auto b = aaxBlock (clock, lowest + (real - 324));
            b.loop = { true, true, static_cast<double> (real) / 24000.0, 0.0, 2.0, 120.0 };
            clock += 256;
            position.apply (b, rate);
            return b;
        };
        for (const std::int64_t head : { 45252, 46276, 47300 })
            for (int i = 0; i < 4; ++i) seen (head);
        std::int64_t head = 324;
        for (; head + 768 < 48000; head += 1024)
            for (int i = 0; i < 4; ++i) seen (head);
        for (int i = 0; i < 3; ++i) seen (head);
        const auto past = seen (head);
        require (! past.projectValid && ! past.positionInterpolated && ! position.proven(), "a wrap below the range ends the proof");
    }
    // The clock's own end: no next callback, no proof carried into it.
    {
        AaxChunkPosition position;
        for (int i = 0; i < 4; ++i) { auto b = aaxBlock (highest - 1024 + i * 256, 0); position.apply (b, rate); }
        auto b = aaxBlock (highest - 255, 1024);
        position.apply (b, rate);
        require (! position.proven(), "a clock at its end proves nothing");
    }
}

static void aaxChunkLosesItsProofAtEveryBreak()
{
    using namespace aax_chunk_test;
    const auto breaks = [] (const char* why, auto edit)
    {
        AaxChunkPosition position;
        std::int64_t clock = 1000, project = 0;
        require (aaxChunkProven (position, clock, project), "the fixture proves a chunk");
        auto head = aaxBlock (clock, project); position.apply (head, rate);
        auto sub = aaxBlock (clock + 256, project); position.apply (sub, rate);
        require (sub.positionInterpolated, "the fixture is inside a proven chunk");
        auto bad = aaxBlock (clock + 512, project);
        edit (bad);
        const auto sent = bad;
        position.apply (bad, rate);
        require (! bad.positionInterpolated && bad.project == sent.project && bad.projectValid == sent.projectValid, why);
        auto next = aaxBlock (sent.clock + sent.frames, sent.project, sent.frames);
        position.apply (next, rate);
        require (! next.positionInterpolated, "the proof does not survive the break");
    };
    breaks ("a seek inside a chunk is the host's own number", [] (BlockClock& b) { b.project += 4800; });
    breaks ("the position moved by one frame", [] (BlockClock& b) { b.project += 1; });
    breaks ("a moved clock is refused", [] (BlockClock& b) { b.clock += 1; });
    breaks ("a changed block length is refused", [] (BlockClock& b) { b.frames = 512; });
    breaks ("a stop ends the proof", [] (BlockClock& b) { b.playing = false; });
    breaks ("a gap ends the proof", [] (BlockClock& b) { b.afterGap = true; });
    breaks ("a host restart ends the proof", [] (BlockClock& b) { b.transportRestart = true; });
    breaks ("a missing clock ends the proof", [] (BlockClock& b) { b.clockValid = false; });
    breaks ("a missing position ends the proof", [] (BlockClock& b) { b.projectValid = false; });

    AaxChunkPosition position;
    std::int64_t clock = 1000, project = 0;
    require (aaxChunkProven (position, clock, project), "the fixture proves a chunk");
    for (int i = 0; i < 4; ++i)
    {
        auto b = aaxBlock (clock + i * 256, project);
        position.apply (b, rate);
    }
    auto late = aaxBlock (clock + 1024, project);   // the host repeats past the proven chunk
    position.apply (late, rate);
    require (! late.positionInterpolated && late.project == project && late.projectValid,
             "a position repeated past the proven chunk is not named");
}

// Other clocks: the same shape, and every field the class may write stays as the host gave it.
static void aaxChunkLeavesEveryOtherClockExactly()
{
    using namespace aax_chunk_test;
    const auto untouched = [] (ClockBasis basis, ClockAuthority authority, std::uint32_t bound, const char* why)
    {
        AaxChunkPosition position;
        for (int i = 0; i < 40; ++i)
        {
            BlockClock sent;
            sent.clock = 1000 + i * 256; sent.project = (i / 4) * 1024; sent.frames = 256;
            stamp (sent, basis, authority, bound);
            sent.loop.ppq = 3.0;
            auto b = sent;
            position.apply (b, rate);
            require (b.project == sent.project && b.projectValid && ! b.positionInterpolated
                     && LoopContext::identical (b.loop.ppq, sent.loop.ppq), why);
        }
    };
    untouched (ClockBasis::vst3Continuous, ClockAuthority::certifiedContent, 0, "VST3 content clock");
    untouched (ClockBasis::vst3Continuous, ClockAuthority::none, 0, "VST3 continuous time");
    untouched (ClockBasis::audioUnitRender, ClockAuthority::logicRender, 0, "Logic render");
    untouched (ClockBasis::audioUnitRender, ClockAuthority::none, 0, "AU render");
    untouched (ClockBasis::reaperProcessing, ClockAuthority::certifiedProject, 0, "REAPER counter");
    untouched (ClockBasis::pluginFrames, ClockAuthority::none, 0, "private counter");
    untouched (ClockBasis::aaxEngine, ClockAuthority::none, 16383, "an AAX host that is not Pro Tools");
    untouched (ClockBasis::aaxEngine, ClockAuthority::boundedAaxEngine, 0, "Pro Tools without a delay bound");

    AaxChunkPosition position;
    for (int i = 0; i < 40; ++i)
    {
        auto b = aaxBlock (1000 + i * 1024, i * 1024, 1024);
        position.apply (b, rate);
        require (b.project == i * 1024 && b.projectValid && ! b.positionInterpolated, "linear Pro Tools blocks pass unchanged");
    }
    // A loop as long as a callback repeats its position for good, and it is no chunk: every lap
    // is a wrap. Nothing closes, so the host's numbers pass, whatever the ordinary rules say.
    AaxChunkPosition lap;
    for (int i = 0; i < 40; ++i)
    {
        auto b = aaxBlock (1000 + i * 256, 1024);
        b.loop = { true, true, 1024.0 / 24000.0, 1024.0 / 24000.0, 1280.0 / 24000.0, 120.0 };
        const auto sent = b;
        lap.apply (b, rate);
        require (b.project == sent.project && b.projectValid && ! b.positionInterpolated && b.chunkCycleSamples == 0
                 && LoopContext::identical (b.loop.ppq, sent.loop.ppq) && ! lap.proven(), "a one-callback loop passes unchanged");
    }
}
