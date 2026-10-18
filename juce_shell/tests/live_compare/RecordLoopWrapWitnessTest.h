#pragma once
#include "../../src/live_compare/RecordLoopWrapWitness.h"
#include "LogicPpqGridTest.h"

// A Keep names a later pass `loop_wrap` only when the host's own loop range names the return
// (INV-R10). Every normal wrap is one, on Logic's PPQ grid as on an exact host; a seek back, a stop
// and restart, an edited loop or a playback without a loop never is.
namespace record_loop_wrap
{
using namespace logic_ppq_grid;

inline bool observe (RecordLoopWrapWitness& witness, const BlockClock& block, double rate)
{
    return witness.observe (block.loop, block.playing, block.projectValid, block.project, block.frames, rate, 0);
}

// Counts the callbacks that begin a host wrap and the ones the witness names.
inline void verifyCycle (LogicHost host, int blocks)
{
    RecordLoopWrapWitness witness;
    int wraps = 0, named = 0;
    for (int i = 0; i < blocks; ++i)
    {
        const auto before = host.project;
        const auto block = host.block();
        if (observe (witness, block, host.rate)) ++named;
        host.advance (host.frames);
        if (i + 1 < blocks && host.project < before + host.frames) ++wraps;
    }
    // The last block's wrap (if any) is never observed; every observed one is named.
    require (wraps > 1, "the walk crosses the loop more than once");
    require (named == wraps, "every normal LOOP wrap is named as the host's own wrap");
}

// A node with presentation delay on Studio One / Studio Pro (LiveCompareBlockClock.h). Content
// position `content` runs on the loop; VST3 clamps the sample to the loop start for the delayed
// tail while PPQ counts the tail before the start, AU clamps PPQ to the start at the wrap while
// the sample stays one lap before.
struct DelayedNode
{
    double rate = 48000, bpm = 120, end = 8;
    std::int64_t frames = 512, delay = 4096, content = 0;
    bool au = false;
    double perBeat() const { return 60.0 * rate / bpm; }
    std::int64_t lap() const { return std::llround (end * perBeat()); }
    LoopContext loopAt (double ppq) const { return { true, true, ppq, 0.0, end, bpm }; }
    // Reported (position, PPQ) for the callback that starts at `content`.
    std::pair<std::int64_t, double> report (std::int64_t at) const
    {
        if (! au && at >= lap() - delay) return { 0, static_cast<double> (at - lap()) / perBeat() };
        return { at, static_cast<double> (at) / perBeat() };
    }
};

inline void verifyDelayedBoundaries()
{
    for (bool au : { false, true })
    for (std::int64_t delay : { 0, 4096 })
    {
        DelayedNode node; node.au = au; node.delay = delay;
        RecordLoopWrapWitness witness;
        std::int64_t content = node.lap() - 24 * node.frames;
        int named = 0, expected = 0;
        for (int i = 0; i < 48; ++i)
        {
            auto [position, ppq] = node.report (content);
            const bool afterWrap = i > 0 && content < node.frames;
            if (au && afterWrap)
            {
                // The wrap callback: PPQ at the start, the sample one lap before the plain wrap.
                ppq = 0.0;
                position = content - node.lap();
            }
            const bool clamped = ! au && delay > 0 && content >= node.lap() - delay;
            if (i > 0 && (clamped || afterWrap)) ++expected;
            if (witness.observe (node.loopAt (ppq), true, true, position, node.frames, node.rate, delay)) ++named;
            content += node.frames;
            if (content >= node.lap()) content -= node.lap();
        }
        // VST3 without a delay is the plain wrap; AU names its clamped wrap whatever the delay.
        require (named == expected, "every delayed boundary callback of the loop is the host's wrap");
    }
    // The tail is bounded by the node's own delay; a seek to the start is no clamp.
    DelayedNode node;
    RecordLoopWrapWitness witness;
    const auto near = node.lap() - 2 * node.delay;
    witness.observe (node.loopAt (static_cast<double> (near) / node.perBeat()), true, true, near,
                     node.frames, node.rate, node.delay);
    const auto at = near + node.frames;
    require (! witness.observe (node.loopAt (static_cast<double> (at - node.lap()) / node.perBeat()),
                                true, true, 0, node.frames, node.rate, node.delay),
             "a clamp longer than the reported delay is no wrap");
    RecordLoopWrapWitness seek;
    seek.observe (node.loopAt (2.0), true, true, 2 * 24000, node.frames, node.rate, node.delay);
    require (! seek.observe (node.loopAt (0.0), true, true, 0, node.frames, node.rate, node.delay),
             "a seek from the middle of the loop to its start is no wrap");
}

inline void verifyRecordLoopWrapWitness()
{
    int cases = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    for (double bpm : { 90.0, 137.5 })
    for (int frames : { 512, 2048 })
    for (bool logic : { true, false })
    {
        LogicHost host; host.rate = rate; host.bpm = bpm; host.frames = frames;
        host.quantize = logic; host.logic = logic;
        host.project = host.loopStart() - 3 * frames;
        const auto lap = host.loopEnd() - host.loopStart();
        verifyCycle (host, static_cast<int> (3 * lap / frames) + 8);

        for (auto fault : { Fault::seekBack, Fault::seekForward, Fault::stop, Fault::range, Fault::tempo })
        {
            auto faulty = host;
            RecordLoopWrapWitness witness;
            for (int i = 0; i < 8; ++i) { observe (witness, faulty.block(), rate); faulty.advance (frames); }
            require (! observe (witness, inject (faulty, fault), rate), "a seek, stop or loop edit is no wrap");
        }
        // A seek back to the loop start from the middle of the loop is not the loop playing.
        auto seek = host;
        RecordLoopWrapWitness witness;
        seek.project = seek.loopStart() + lap / 2;
        observe (witness, seek.block(), rate);
        seek.project = seek.loopStart();
        require (! observe (witness, seek.block(), rate), "a seek to the loop start is no wrap");
        // A host that plays without a loop never names a wrap, even when it jumps back.
        auto linear = host;
        RecordLoopWrapWitness unlooped;
        auto first = linear.block(); first.loop.active = false;
        observe (unlooped, first, rate);
        linear.project -= lap - frames;
        auto back = linear.block(); back.loop.active = false;
        require (! observe (unlooped, back, rate), "no loop, no wrap");
        ++cases;
    }
    verifyDelayedBoundaries();
    std::printf ("Record LOOP wrap witness: %d rate/tempo/block/profile cases PASS\n", cases);
}
}
