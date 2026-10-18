#pragma once
#include "LogicPpqGridTest.h"

// A Keep names a later pass `loop_wrap` only when the host's own loop range names the return
// (INV-R10). Every normal wrap is one, on Logic's PPQ grid as on an exact host; a seek back, a stop
// and restart, an edited loop or a playback without a loop never is.
namespace record_loop_wrap
{
using namespace logic_ppq_grid;

inline bool observe (RecordLoopWrapWitness& witness, const BlockClock& block, double rate)
{
    return witness.observe (block.loop, block.playing, block.projectValid, block.project, block.frames, rate);
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
    std::printf ("Record LOOP wrap witness: %d rate/tempo/block/profile cases PASS\n", cases);
}
}
