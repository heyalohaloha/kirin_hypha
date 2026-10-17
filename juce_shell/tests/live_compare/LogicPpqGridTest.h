#pragma once
#include "../../src/live_compare/LiveCompareRing.h"
#include "../../src/live_compare/PairedAnalysisClock.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Logic Pro reports its musical position on a grid of 1/15360 quarter note while its render
// clock and sample position advance exactly. The model below rounds the PPQ of every callback
// to that grid (ceil, as measured, and floor/round so the rule does not depend on the mode).
// Normal cycle playback must never be reported as a transport break, and every real break
// (seek, stop, tempo/range edit, playback rate change, unexplained move, clock move) must be.
namespace logic_ppq_grid
{
using namespace hypha::live_compare;
inline void require (bool ok, const char* why)
{ if (! ok) { std::fprintf (stderr, "Logic PPQ grid: %s\n", why); std::exit (1); } }

enum class Rounding { ceil, floor, nearest };
enum class Fault { none, seekForward, seekBack, stop, tempo, range, rate, projectOnly, ppqOnly, clock };

struct LogicHost
{
    double rate = 96000, bpm = 240, startBeat = 0, endBeat = 16;
    int frames = 2048;
    Rounding rounding = Rounding::ceil;
    bool quantize = true, logic = true;
    std::int64_t clock = 7777, project = 0;

    double samplesPerBeat() const { return 60.0 * rate / bpm; }
    std::int64_t loopStart() const { return std::llround (startBeat * samplesPerBeat()); }
    std::int64_t loopEnd() const { return std::llround (endBeat * samplesPerBeat()); }
    double ppqOf (std::int64_t position) const
    {
        const double beats = static_cast<double> (position) / samplesPerBeat();
        if (! quantize) return beats;
        const double ticks = beats * 15360.0;
        const double q = rounding == Rounding::ceil ? std::ceil (ticks - 1.0e-7)
                       : rounding == Rounding::floor ? std::floor (ticks + 1.0e-7) : std::round (ticks);
        return q / 15360.0;
    }
    BlockClock block() const
    {
        BlockClock b;
        b.clock = clock; b.project = project; b.frames = frames;
        b.clockValid = b.projectValid = b.playing = true;
        b.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
        b.clockAuthority = static_cast<std::uint8_t> (logic ? ClockAuthority::logicRender : ClockAuthority::none);
        b.presentationSource = 2; b.outputPresentationValid = true; b.outputPresentationSamples = 685;
        b.loop = { true, true, ppqOf (project), startBeat, endBeat, bpm };
        return b;
    }
    // The host's own timeline: the cycle wraps inside a block at its integer end sample.
    void advance (std::int64_t timelineFrames)
    {
        clock += frames;
        project += timelineFrames;
        while (project >= loopEnd()) project -= loopEnd() - loopStart();
    }
};

inline BlockClock inject (LogicHost& host, Fault fault)
{
    switch (fault)
    {
        case Fault::seekForward: host.project += host.frames / 4 + 5; break;
        case Fault::seekBack: host.project -= std::llround (host.samplesPerBeat()); break;
        case Fault::tempo: host.bpm *= 1.01; break;
        case Fault::range: host.endBeat -= 4; break;
        case Fault::clock: host.clock += 1; break;
        // A playback rate change moves the timeline 2 % faster than the render clock.
        case Fault::rate: host.project += host.frames / 50; break;
        default: break;
    }
    auto b = host.block();
    const double grid = host.samplesPerBeat() / 15360.0;
    if (fault == Fault::stop) b.playing = false;
    // Logic's project is exact: the smallest move beyond its one-sample pre-roll step is a break.
    if (fault == Fault::projectOnly) b.project += 2;
    // A PPQ-only contradiction is observable once it exceeds the allowance (one sample plus two
    // grid steps) and the error of the two readings themselves (two more grid steps).
    if (fault == Fault::ppqOnly)
        b.loop.ppq += std::ceil ((2.0 + 4.0 * grid) / grid) / 15360.0;
    return b;
}

inline TimelineBreak expectedCause (Fault fault)
{
    return fault == Fault::stop ? TimelineBreak::stopped
         : fault == Fault::tempo || fault == Fault::range ? TimelineBreak::loopEdited
         : fault == Fault::clock ? TimelineBreak::clockMoved : TimelineBreak::projectMoved;
}

// Two full laps and a block that straddles each wrap; no false break, no WAIT.
inline int steadyBreaks (LogicHost host, int blocks)
{
    LoopTimeline timeline;
    int broken = 0;
    for (int i = 0; i < blocks; ++i)
    {
        const auto step = timeline.observe (host.block(), host.rate);
        if (i > 0 && (step.broken || step.waiting)) ++broken;
        host.advance (host.frames);
    }
    return broken;
}

inline void verifyLogicPpqGrid()
{
    int cases = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    for (double bpm : { 90.0, 120.0, 137.5, 240.0 })
    for (int frames : { 512, 2048 })
    for (auto rounding : { Rounding::ceil, Rounding::floor, Rounding::nearest })
    {
        LogicHost host; host.rate = rate; host.bpm = bpm; host.frames = frames; host.rounding = rounding;
        host.project = host.loopStart() - 3 * frames;
        const auto lap = host.loopEnd() - host.loopStart();
        const int blocks = static_cast<int> (3 * lap / frames) + 8;
        require (steadyBreaks (host, blocks) == 0, "normal Logic cycle playback is never a transport break");

        // The PRE ring keeps ONE run through every verified wrap (INV-LC2), and the analysis
        // clock keeps one generation, so the delayed POST reader is never fenced off.
        auto ring = std::make_unique<Ring>(); ring->initialise (1, static_cast<std::uint32_t> (rate));
        Publisher publisher; PairedAnalysisClock analysis;
        std::array<float, 2048> silence {}; const float* in[] { silence.data() };
        auto walk = host;
        std::uint64_t run = 0, generation = 0;
        for (int i = 0; i < blocks; ++i)
        {
            const auto b = walk.block();
            publisher.publish (*ring, b, in, 1);
            analysis.observeLocal (b, rate, true);
            if (i == 1) { run = ring->header.run.load(); generation = analysis.local.revision; }
            if (i > 1)
            {
                require (ring->header.run.load() == run, "PRE run survives every verified cycle wrap");
                require (analysis.local.valid && analysis.local.revision == generation,
                         "analysis clock keeps one generation through the cycle");
            }
            walk.advance (walk.frames);
        }

        for (auto fault : { Fault::seekForward, Fault::seekBack, Fault::stop, Fault::tempo, Fault::range,
                            Fault::rate, Fault::projectOnly, Fault::ppqOnly, Fault::clock })
        {
            auto faulty = host;
            LoopTimeline timeline;
            for (int i = 0; i < blocks / 2; ++i)
            { timeline.observe (faulty.block(), rate); faulty.advance (faulty.frames); }
            const auto step = timeline.observe (inject (faulty, fault), rate);
            require (step.broken && ! step.waiting, "every real discontinuity is reported");
            require (step.cause == expectedCause (fault), "the discontinuity keeps its own cause");
        }
        ++cases;
    }

    // The grid is a property of Logic's clock, not a wider tolerance for every host or reading.
    for (int frames : { 512, 2048 })
    {
        LogicHost other; other.frames = frames; other.logic = false;
        other.project = other.loopStart();
        require (steadyBreaks (other, 400) > 0, "another AU host keeps the one-sample PPQ rule");
        LogicHost exact; exact.frames = frames; exact.quantize = false;
        require (steadyBreaks (exact, 400) == 0, "exact Logic PPQ still passes");
        // A reading 1.4 samples away and OFF the grid is not explained by Logic's rounding.
        LogicHost grid; grid.frames = frames; grid.project = grid.loopStart();
        LoopTimeline timeline;
        for (int i = 0; i < 40; ++i) { timeline.observe (grid.block(), grid.rate); grid.advance (frames); }
        const auto previous = grid.block();
        auto off = previous;
        off.clock += frames; off.project += frames;
        off.loop.ppq = previous.loop.ppq + off.loop.beats (frames, grid.rate) + 1.4 / grid.samplesPerBeat();
        timeline.reset();
        timeline.observe (previous, grid.rate);
        require (timeline.observe (off, grid.rate).broken, "an off-grid Logic reading gets no grid allowance");
    }
    std::printf ("Logic PPQ grid: %d rate/tempo/block/rounding cases PASS\n", cases);
}
}
