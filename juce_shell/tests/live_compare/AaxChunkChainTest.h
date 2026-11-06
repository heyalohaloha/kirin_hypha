#pragma once
#include "AaxChunkLoopTest.h"
#include "AaxChunkPositionTest.h"

// PRE's named callbacks are told apart wherever PRE's timing is read.
static void aaxChunkKeepsPreReportsApartFromNamedPositions()
{
    using namespace aax_chunk_test;
    {
        auto header = std::make_unique<TimingHeader>();
        TimingPublisher publisher;
        AaxChunkPosition position;
        std::uint16_t reports = 0;
        TimingSnapshot snapshot;
        for (int chunk = 0; chunk < 6; ++chunk)
            for (int i = 0; i < 4; ++i)
            {
                auto b = aaxBlock (1000 + chunk * 1024 + i * 256, 5000 + chunk * 1024);
                position.apply (b, rate);
                const auto named = b.positionInterpolated;
                const auto active = b.projectValid;
                publisher.observe (*header, b, rate);
                require (readTiming (*header, snapshot), "the fixture reads its own timing");
                if (active && ! named) ++reports;
                require (snapshot.block.positionInterpolated == named && snapshot.reports == reports,
                         "the named attribute and the report count reach POST");
                if (snapshot.anchor.linearKnown) require (snapshot.origin == 1000 - 5000, "the origin stays the host's");
            }
        require (reports == 9, "the first chunk's four reports, then one per chunk");
    }
    // A generation that begins on a named callback has no anchor until a host report.
    {
        auto header = std::make_unique<TimingHeader>();
        TimingPublisher publisher;
        const LoopContext loop { true, true, 0.5, 0.0, 2.0, 120.0 };
        auto named = aaxBlock (1000, 12000); named.loop = loop; named.positionInterpolated = true;
        publisher.observe (*header, named, rate);
        TimingSnapshot snapshot;
        require (readTiming (*header, snapshot) && ! snapshot.anchor.loopKnown && ! snapshot.anchor.linearKnown,
                 "a named callback anchors nothing");
        auto report = aaxBlock (1256, 12256); report.loop = loop; report.loop.ppq = 12256.0 / 24000.0;
        publisher.observe (*header, report, rate);
        require (readTiming (*header, snapshot) && snapshot.anchor.loopKnown && snapshot.anchor.clock == 1256
                 && snapshot.anchor.project == 12256, "the loop anchor is a host report");
    }
    // The linear origin as well: a named callback after one report leaves it unknown.
    {
        auto header = std::make_unique<TimingHeader>();
        TimingPublisher publisher;
        auto report = aaxBlock (1000, 5000);
        publisher.observe (*header, report, rate);
        auto named = aaxBlock (1256, 5256); named.positionInterpolated = true;
        publisher.observe (*header, named, rate);
        TimingSnapshot snapshot;
        require (readTiming (*header, snapshot) && ! snapshot.anchor.linearKnown, "a named callback completes no origin");
        auto next = aaxBlock (1512, 5512);
        publisher.observe (*header, next, rate);
        require (readTiming (*header, snapshot) && snapshot.anchor.linearKnown && snapshot.origin == 1512 - 5512,
                 "the next report does");
    }
    // The initial loop proof reads PRE's reported anchor, never its named position.
    {
        LoopAnchor anchor;
        anchor.loopKnown = true; anchor.loopSamples = 48000; anchor.clock = 1000; anchor.project = 12000; anchor.runStart = 1000;
        anchor.loop = { true, true, 0.5, 0.0, 2.0, 120.0 };
        auto pre = aaxBlock (2024, 13024, 1024); pre.loop = anchor.loop; pre.loop.ppq = 13024.0 / 24000.0;
        auto post = aaxBlock (1524, 12012); post.loop = anchor.loop; post.loop.ppq = 12012.0 / 24000.0;
        const auto reported = initialLoopCandidate (anchor, pre, post, 48000, rate, ringCapacityFrames);
        require (reported.valid && reported.k == 512, "the fixture proves K from PRE's report");
        pre.positionInterpolated = true; pre.project += 100; pre.loop.ppq += 100.0 / 24000.0;
        const auto named = initialLoopCandidate (anchor, pre, post, 48000, rate, ringCapacityFrames);
        require (named.k == 512, "a named PRE position does not move K");
    }
    // A PCM run that begins between two reports takes its origin from the next report.
    {
        auto ring = std::make_unique<Ring>();
        ring->initialise (key, rate);
        PreFeeder feeder;
        AaxChunkPosition position;
        std::array<float, 256> left {}, right {};
        const float* in[] { left.data(), right.data() };
        std::int64_t clock = 1000;
        const auto feed = [&] (std::int64_t project)
        {
            auto b = aaxBlock (clock, project);
            clock += 256;
            position.apply (b, rate);
            feeder.feed (*ring, b, in, 2);
            return b;
        };
        for (const std::int64_t head : { 5000, 6024 })
            for (int i = 0; i < 4; ++i) feed (head);
        feed (7048);
        ring->header.demand.store (1);
        const auto first = feed (7048);
        require (first.positionInterpolated, "demand rises between two reports");
        const auto start = ring->header.runStart.load();
        require ((ring->header.anchorFlags.load() & 1u) == 0, "a named callback is no run origin");
        feed (7048); feed (7048);
        require ((ring->header.anchorFlags.load() & 1u) == 0, "nor are the callbacks after it");
        const auto report = feed (8072);
        require (! report.positionInterpolated && ring->header.anchorFlags.load() == 1u
                 && ring->header.runProject.load() == 8072 - (report.clock - start), "the next report anchors the run");
    }
    // Only PRE splits, PRE's chunks in another phase, and POST reporting far more often than PRE:
    // eight new reports from each side, whatever the callbacks in between.
    const struct { int preFrames, postFrames, phase; bool postLinear; std::int64_t k; const char* name; } cases[] = {
        { 256, 1024, 0, false, 1024, "only PRE splits" },
        { 256, 256, 256, false, 0, "PRE's chunks a quarter behind POST's" },
        { 256, 256, 512, false, 0, "PRE's chunks half behind POST's" },
        { 64, 64, 0, true, 0, "POST reports sixteen times per PRE report" },
    };
    for (const auto& c : cases)
    {
        Options options;
        options.preFrames = c.preFrames; options.postFrames = c.postFrames; options.prePhase = c.phase;
        options.postLinear = c.postLinear; options.k = c.k;
        Chain chain (options);
        chain.run (60);
        std::printf ("%s: K %lld, audible %d, first chunk %d\n", c.name, static_cast<long long> (chain.decision.k),
                     chain.proven, chain.firstAcceptChunk);
        require (chain.decision.kValid && chain.decision.k == c.k && chain.proven > 0, c.name);
        require (chain.wrongAddress == 0 && chain.wrongPcm == 0 && chain.dropouts == 0, c.name);
        require (chain.firstAcceptChunk >= 8, "PRE's named callbacks add no calibration");
    }
}

// Without a split the rule must be invisible. The same host with and without it, callback by
// callback: linear playback, a known K into loops as short as a callback, a seek, a longer loop.
static void aaxChunkChangesNoBitWithoutASplit()
{
    using namespace aax_chunk_test;
    const auto script = [] (bool named)
    {
        Options options;
        options.preFrames = options.postFrames = 256;
        options.preLinear = options.postLinear = true;
        options.k = 512; options.named = named; options.record = true;
        auto chain = std::make_unique<Chain> (options);
        chain->run (30);
        for (const std::int64_t loop : { 256, 512, 1024, 2048, 48000 })
        {
            chain->enterLoop (loop);
            chain->run (30);
            chain->leaveLoop();
            chain->run (20);
            chain->relocate (4800);
            chain->run (20);
        }
        return chain;
    };
    const auto without = script (false), with = script (true);
    std::printf ("no split: %zu callbacks, %d accepted\n", without->log.size(), without->accepted);
    require (without->accepted > 0 && without->log == with->log, "without a split the rule changes no decision");
}

// The renderer's own count, with no adoption: eight new PRE reports, as for the observer.
static void aaxChunkCountsEachPreReportOnce()
{
    using namespace aax_chunk_test;
    Options options;
    options.preFrames = options.postFrames = 64;
    options.postLinear = true; options.k = 0; options.adopt = false;
    Chain chain (options);
    chain.run (60);
    require (chain.proven > 0 && chain.wrongPcm == 0 && chain.firstAcceptChunk >= 8,
             "sixteen POST reports against one PRE report count once");
}

// A candidate read across a PRE write is no observation; the next undisturbed one counts.
static void aaxChunkCountsNoObservationDuringAWrite()
{
    using namespace aax_chunk_test;
    auto ring = std::make_unique<Ring>();
    ring->initialise (key, rate);
    Publisher publisher;
    Consumer consumer;
    std::array<float, 256> left {}, right {};
    const float* in[] { left.data(), right.data() };
    float* out[] { left.data(), right.data() };
    std::int64_t clock = 1000;
    const auto step = [&] (bool racing)
    {
        const auto pre = aaxBlock (clock, clock + 500);
        publisher.publish (*ring, pre, in, 2);
        const auto seq = ring->header.seq.load();
        if (racing) ring->header.seq.store (seq + 1);
        const auto post = aaxBlock (clock, clock + 500);
        const auto decision = consumer.process (*ring, key, rate, post, out, 2);
        ring->header.seq.store (seq);
        clock += 256;
        return decision;
    };
    for (int i = 0; i < 8; ++i) require (! step (false).kValid, "seven observations prove nothing yet");
    const auto raced = step (true);
    require (! raced.candidateSeen && ! raced.kValid, "the racing candidate is not read");
    require (step (false).kValid, "the eighth undisturbed observation proves K");
}

// The measured shapes in a whole chain: K is the position difference, PRE reads that address.
static void aaxChunkProvesKAndReadsTheRightPre()
{
    using namespace aax_chunk_test;
    const struct { int frames; std::int64_t k; const char* name; } cases[] = {
        { 256, -512, "256 frames, H/W Insert Delay 0 (measured)" },
        { 256, -128, "256 frames, H/W Insert Delay 8 ms (measured)" },
        { 512, 0, "512 frames, H/W Insert Delay 0 (measured)" },
        { 256, 512, "256 frames, H/W Insert Delay 21.33 ms" },
        { 512, 1024, "512 frames, H/W Insert Delay 21.33 ms" },
        { 128, 512, "128 frames" },
        { 64, 2048, "64 frames" },
        { 1024, 2048, "1024 frames, no split (measured)" },
    };
    for (const auto& c : cases)
    {
        Options options;
        options.postFrames = c.frames; options.k = c.k;
        Chain chain (options);
        chain.run (60);
        std::printf ("%s: K %lld, accepted %d, audible %d, first chunk %d, dropouts %d\n", c.name,
                     static_cast<long long> (chain.decision.k), chain.accepted, chain.proven, chain.firstAcceptChunk, chain.dropouts);
        require (chain.decision.kValid && chain.decision.k == c.k, "K is the difference of the host's positions");
        require (chain.wrongAddress == 0 && chain.wrongPcm == 0, "every accepted block is the PRE at clock minus K");
        require (chain.accepted > 0 && chain.proven > 0 && chain.dropouts == 0, "once proven PRE stays on");
        // Eight host reports, as in every other Pro Tools case: named positions add no evidence.
        require (chain.firstAcceptChunk >= 8, "named callbacks never count as calibration");
        require (chain.firstAcceptChunk <= 12, "the wait stays short");
    }
    // PRE at the hardware buffer as well (unmeasured): the same rule on each side, the same K.
    {
        Options options;
        options.preFrames = 256; options.k = 0;
        Chain chain (options);
        chain.run (60);
        require (chain.decision.kValid && chain.decision.k == 0 && chain.proven > 0 && chain.dropouts == 0
                 && chain.wrongAddress == 0 && chain.wrongPcm == 0, "both sides split into one chain");
    }
    // The control: without the rule the measured failure returns and PRE never sounds.
    for (const int frames : { 256, 512 })
    {
        Options options;
        options.postFrames = frames; options.k = frames == 256 ? -512 : 0; options.named = false;
        Chain chain (options);
        chain.run (60);
        require (chain.accepted == 0 && ! chain.decision.kValid, "the host's raw repeats prove nothing");
    }
}

static void aaxChunkKeepsEveryExistingFence()
{
    using namespace aax_chunk_test;
    const auto proven = []
    {
        auto chain = std::make_unique<Chain> (Options {});
        chain->run (30);
        require (chain->decision.kValid && chain->proven > 0, "the fixture sounds PRE");
        return chain;
    };
    const auto fencedAt = [&proven] (auto edit, const char* why)
    {
        auto chain = proven();
        // The callback that shows the change is POST at once, and so is the rest of the chunk.
        chain->chunk ([&] (int i, BlockClock& b)
        {
            if (i == 1) edit (b);
            if (i == 1 || i == 0) return;
            b.playing = false;
        });
        require (chain->dropouts >= 3 && chain->wrongAddress == 0 && chain->wrongPcm == 0, why);
    };
    fencedAt ([] (BlockClock& b) { b.clock += 1; }, "a clock that jumps is POST");
    fencedAt ([] (BlockClock& b) { b.frames = 512; }, "a changed block length is POST");
    fencedAt ([] (BlockClock& b) { b.playing = false; }, "a stop is POST");
    // Inside a chunk: the block that shows the change is POST at once, with no old K.
    {
        auto chain = proven();
        chain->chunk ([] (int i, BlockClock& b) { if (i == 2) b.project += 4800; });
        require (! chain->decision.kValid && chain->dropouts >= 2, "a seek inside a chunk drops K at that block");
    }
    // At a chunk boundary: a step that is not the chunk.
    {
        auto chain = proven();
        chain->chunk ([] (int i, BlockClock& b) { if (i == 0) b.project += 1; });
        require (chain->dropouts > 0 && ! chain->decision.kValid, "a step one frame off drops K");
    }
    // A seek keeps K only by proving it again; a delay change must prove the new K.
    {
        auto chain = proven();
        chain->relocate (48000);
        chain->run (30);
        require (chain->dropouts >= 4 * 8 && chain->decision.kValid && chain->decision.k == -512,
                 "after a seek eight host reports prove K again");
        require (chain->wrongAddress == 0 && chain->wrongPcm == 0, "no block sounds across the seek");
    }
    {
        auto chain = proven();
        chain->relocate (0, 256);
        chain->run (30);
        require (chain->decision.kValid && chain->decision.k == -256, "the changed compensation proves its new K");
        require (chain->wrongAddress == 0 && chain->wrongPcm == 0, "the old K never reads after the delay changed");
    }
    // Another clock with the same shape stays refused, exactly as before.
    for (const auto basis : { ClockBasis::vst3Continuous, ClockBasis::audioUnitRender, ClockBasis::pluginFrames })
    {
        Options options;
        options.basis = basis; options.authority = ClockAuthority::none; options.bound = 0;
        Chain chain (options);
        chain.run (60);
        require (chain.accepted == 0 && ! chain.decision.kValid, "only the certified DAE clock may split");
    }
    Options uncertified;
    uncertified.authority = ClockAuthority::none;
    Chain plain (uncertified);
    plain.run (60);
    require (plain.accepted == 0, "an AAX host that is not Pro Tools stays refused until it is measured");
}

// PRE at 1024 and POST at the hardware buffer: nothing unwritten, nothing past the ring.
static void aaxChunkNeverReadsUnwrittenPre()
{
    using namespace aax_chunk_test;
    // The measured order: PRE one chunk ahead, so even K = -512 reads only what was written.
    {
        Options options; // K -512
        Chain chain (options);
        chain.run (200);
        require (chain.dropouts == 0, "the measured schedule never reads past PRE's write end");
    }
    // Further ahead than PRE has written: never proven, never audible.
    for (const std::int64_t k : { std::int64_t (-2048), std::int64_t (-4096) })
    {
        Options options;
        options.k = k;
        Chain chain (options);
        chain.run (60);
        require (chain.accepted == 0, "a K past PRE's write end never sounds");
    }
    // PRE scheduled after POST's chunk: the unwritten part of the chunk is POST, block by block.
    {
        Options options;
        options.preAfterPost = true; options.k = 512;
        Chain chain (options);
        chain.run (60);
        require (chain.wrongAddress == 0 && chain.wrongPcm == 0, "only written PRE is read");
        Options behind = options;
        behind.k = 1024;
        Chain written (behind);
        written.run (60);
        require (written.accepted > 0 && written.dropouts == 0 && written.wrongPcm == 0, "one chunk behind is fully written");
    }
    // Older than the ring: never proven and never audible.
    {
        Options options;
        options.k = static_cast<std::int64_t> (ringCapacityFrames) + 2048;
        options.bound = 16383;
        Chain chain (options);
        chain.run (1100);
        require (chain.accepted == 0, "a K beyond the ring never sounds");
    }
}

// INV-LC25: elapsed time is only counted between blocks of the same length.
static void aaxChunkAddsNoChainTiming()
{
    using namespace aax_chunk_test;
    for (const bool feeding : { true, false })
    {
        Options options;
        options.preDemand = feeding;
        Chain chain (options);
        chain.run (120);
        ChainTimingReport report;
        require (chain.chainMeter.read (report) && report.callbacks > 0, "the fixture closes a window");
        require (report.blocks == 0, "PRE at 1024 and POST at 256 are never counted");
        require (feeding || report.rejected[static_cast<std::size_t> (ChainTimingReason::unevenBlocks)] > 0,
                 "without a PCM copy the block lengths are what refuses it");
    }
}

static void aaxChunkPositionContract()
{
    aaxChunkIsNamedOnlyAfterItCloses();
    aaxChunkRefusesEveryOtherShapeBeforeItIsProven();
    aaxChunkMeasuresTheCycleBeforeNamingPastAWrap();
    aaxChunkKeepsTheCycleToItsLoop();
    aaxChunkKeepsAnEarlyWrapForItsProof();
    aaxChunkChangesNoBitWithoutASplit();
    aaxChunkCountsEachPreReportOnce();
    aaxChunkCountsNoObservationDuringAWrite();
    aaxChunkRefusesAContradictoryPpq();
    aaxChunkProofEndsWithItsEngine();
    aaxChunkArithmeticNeverOverflows();
    aaxChunkKeepsPreReportsApartFromNamedPositions();
    aaxChunkLosesItsProofAtEveryBreak();
    aaxChunkLeavesEveryOtherClockExactly();
    aaxChunkProvesKAndReadsTheRightPre();
    aaxChunkKeepsEveryExistingFence();
    aaxChunkNeverReadsUnwrittenPre();
    aaxChunkFollowsTheHostsLoop();
    aaxChunkAddsNoChainTiming();
    static_assert (sizeof (AaxChunkPosition) <= 256, "the split keeps a small fixed state per instance");
}
