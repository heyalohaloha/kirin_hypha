#pragma once
#include "AaxChunkChain.h"

#include <cmath>
#include <limits>

// INV-LC22 in the same shape. PRE reports its wraps; POST's are measured from the first one.
static void aaxChunkFollowsTheHostsLoop()
{
    using namespace aax_chunk_test;
    Options options;
    options.k = 512; options.loopFrames = 48000;
    Chain chain (options);
    const auto chunksPerLap = 47;
    chain.run (chunksPerLap * 4);
    std::printf ("loop: K %lld, accepted %d, audible %d, first chunk %d, dropouts %d\n",
                 static_cast<long long> (chain.decision.k), chain.accepted, chain.proven, chain.firstAcceptChunk, chain.dropouts);
    require (chain.decision.kValid && chain.decision.k == 512, "the bounded proof holds with a split POST");
    require (chain.wrongAddress == 0 && chain.wrongPcm == 0, "every accepted loop block is the PRE at clock minus K");
    require (chain.accepted > 0 && chain.dropouts == 0, "a measured wrap keeps the proof");
    require (chain.firstAcceptChunk >= 43 + 8, "no PRE before both sides measured the cycle, then eight host reports");

    // POST's first wrap measures the cycle; then eight host reports, whatever the buffer. At 64
    // frames sixteen named callbacks per chunk would otherwise finish the count at once.
    Options fine = options;
    fine.postFrames = 64;
    Chain small (fine);
    small.run (chunksPerLap * 4);
    require (small.decision.kValid && small.wrongPcm == 0 && small.firstAcceptChunk >= 43 + 8,
             "named callbacks never count towards the initial loop proof");

    // A step that is not the loop's wrap is a seek, whatever the position.
    chain.chunk ([] (int i, BlockClock& b) { if (i == 0) { b.project += 2400; b.loop.ppq += 0.1; } });
    require (! chain.decision.kValid && chain.report.verdict != Verdict::accepted, "a seek inside a loop drops K");

    // A contradictory or missing PPQ inside a proven chunk: POST at once, and fresh proof returns.
    for (const auto ppq : { 0.25, std::numeric_limits<double>::quiet_NaN() })
    {
        Chain contradicted (options);
        contradicted.run (chunksPerLap * 3);
        require (contradicted.decision.kValid && contradicted.proven > 0, "the fixture sounds PRE in the loop");
        const auto before = contradicted.dropouts;
        contradicted.chunk ([ppq] (int i, BlockClock& b) { if (i == 2) b.loop.ppq = std::isnan (ppq) ? ppq : b.loop.ppq + ppq; });
        require (contradicted.dropouts > before && ! contradicted.decision.kValid, "no PRE on a contradicted position");
        contradicted.run (chunksPerLap * 3);
        require (contradicted.decision.kValid && contradicted.decision.k == 512 && contradicted.wrongPcm == 0
                 && contradicted.wrongAddress == 0, "the same K returns on new proof");
    }

    // Into a loop with K already proven: PRE keeps sounding until the callbacks past the first
    // wrap POST cannot name. The next report measures the cycle, so only a moment is POST.
    {
        Options linear;
        linear.k = 512;
        Chain entering (linear);
        entering.run (30);
        require (entering.decision.kValid && entering.decision.k == 512, "the fixture proves K in linear playback");
        const auto proven = entering.proven;
        entering.enterLoop (48000);
        entering.run (10);
        require (entering.proven > proven && entering.decision.kValid && entering.dropouts == 0, "a known K carries into the loop");
        entering.run (9);
        require (! entering.decision.kValid && entering.report.verdict != Verdict::accepted, "the first wrap withholds PRE");
        entering.run (10);
        require (entering.decision.kValid && entering.decision.k == 512 && entering.report.verdict == Verdict::accepted,
                 "the same K returns on the measured cycle, without another lap");
        const auto waited = entering.dropouts;
        require (waited <= 10 * 4, "only a moment around the wrap is POST");
        entering.run (chunksPerLap * 3);
        require (entering.dropouts == waited && entering.wrongAddress == 0 && entering.wrongPcm == 0, "later wraps keep PRE on");
    }
    // The same entry with K below zero: PRE's lead bounds it, so it proves itself the same way.
    {
        Options linear;
        linear.k = -512;
        Chain entering (linear);
        entering.run (30);
        require (entering.decision.kValid && entering.decision.k == -512, "the fixture proves a negative K");
        entering.enterLoop (48000);
        entering.run (10);
        require (entering.decision.kValid && entering.dropouts == 0, "a known negative K carries into the loop");
        entering.run (chunksPerLap * 4);
        require (entering.decision.kValid && entering.decision.k == -512, "a negative K holds through the wraps");
        require (entering.dropouts <= 12 * 4 && entering.wrongAddress == 0 && entering.wrongPcm == 0,
                 "at most a moment around the first wrap is POST");
        const auto audible = entering.proven;
        entering.leaveLoop();
        entering.run (30);
        require (entering.decision.kValid && entering.decision.k == -512 && entering.proven > audible
                 && entering.wrongPcm == 0, "linear playback keeps it");
        Chain moved (linear);
        moved.run (30);
        moved.enterLoop (48000);
        moved.run (chunksPerLap * 2);
        moved.setK (512);
        moved.run (chunksPerLap * 3);
        require (moved.decision.kValid && moved.decision.k == 512 && moved.wrongAddress == 0 && moved.wrongPcm == 0,
                 "a delay that makes K positive proves it inside the loop");
    }

    // From the start of a loop as well, once both sides measured the cycle.
    Options past;
    past.k = -512; past.loopFrames = 48000;
    Chain ahead (past);
    ahead.run (chunksPerLap * 4);
    require (ahead.decision.kValid && ahead.decision.k == -512 && ahead.accepted > 0
             && ahead.wrongAddress == 0 && ahead.wrongPcm == 0, "a negative K proves itself inside a loop");
    auto compact = past; compact.loopFrames = 17000;
    Chain shortUnique (compact);
    shortUnique.run (160);
    require (shortUnique.decision.kValid && shortUnique.decision.k == -512 && shortUnique.accepted > 0
             && shortUnique.wrongAddress == 0 && shortUnique.wrongPcm == 0,
             "a shorter loop still plays the unique written negative candidate");
    // Further below zero than PRE has written, nothing can be read: no proof and no PRE.
    Options unwritten = past;
    unwritten.k = -4096;
    Chain beyond (unwritten);
    beyond.run (chunksPerLap * 4);
    require (beyond.accepted == 0, "a K past PRE's write end stays on POST in a loop");
}
