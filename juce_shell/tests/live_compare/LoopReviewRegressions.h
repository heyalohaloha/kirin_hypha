#pragma once
#include "ReaperFixture.h"
#include "../../src/live_compare/PairedAnalysisClock.h"
namespace loop_review
{
using namespace reaper_fixture;
inline void reaper()
{
    // The proposed block starts in interval one but crosses its actual end, not the global end.
    Host boundary; boundary.length = 384000; boundary.runtimeBound = true;
    for (int i = 0; i < 753; ++i) boundary.publish();
    TimingSnapshot peer; require (readTiming (boundary.ring->header.timing, peer), "split snapshot");
    auto post = boundary.pre; post.project = boundary.length - 256;
    post.loop.ppq = post.project / 48000.0;
    std::int64_t address = 0;
    require (!reaperWindowAddress (peer.anchor, peer.block, post, rate, ringCapacityFrames, address),
             "a copied block cannot cross a native interval end");
    for (int missingSide : { 0, 1 })
    for (bool blind : { false, true })
    {
        Host host; host.length = 384000; host.runtimeBound = true; host.gain = 0.75f;
        for (int i = 0; i < 100; ++i) host.consume();
        require (host.report.preAudible, "expiry fixture begins with audible PRE");
        const auto run = host.ring->header.run.load();
        const auto k = host.decision.k;
        if (blind) host.blind.start (true);
        host.expiredSide = missingSide;
        for (int i = 0; i < 700; ++i)
        {
            host.consume();
            require (!host.report.preAudible && host.decision.kValid && host.decision.k == k,
                     "expiry including a wrap withholds output without revoking K");
        }
        require (host.ring->header.run.load() == run, "expiry preserves the PCM run identity");
        host.expiredSide = -1;
        host.consume();
        if (blind) require (!host.blind.view().invalidated && host.report.preAudible, "same Blind resumes after expiry");
        else require (host.report.preAudible && host.decision.k == k, "same proof immediately resumes named PRE and gain");
    }
    for (int fault = 0; fault < 5; ++fault)
    {
        Host host; host.length = 384000; host.runtimeBound = true;
        for (int i = 0; i < 100; ++i) host.consume();
        host.blind.start (true);
        if (fault < 2) host.missingSide = fault;
        if (fault == 2) host.foreignPost = true;
        if (fault == 3) host.length += 512;
        if (fault == 4) { host.expiredSide = 0; host.consume(); host.runtimeDelay = 4096; }
        host.consume();
        require (!host.report.preAudible && !host.blind.view().invalidated, "missing/foreign/edited proof keeps POST within the same trial");
        host.missingSide = -1; host.foreignPost = false; host.expiredSide = -1;
        for (int i = 0; i < 40; ++i) host.consume();
        require (host.report.preAudible && !host.blind.view().invalidated, "fresh proof resumes the same trial");
    }
    Host unsplit; unsplit.length = 384123; unsplit.runtimeBound = true;
    for (int i = 0; i < 1550; ++i)
    {
        const bool crossing = unsplit.postPosition % unsplit.length + frames > unsplit.length;
        unsplit.consume();
        if (crossing) require (!unsplit.report.preAudible, "unsplit block across loop end is withheld");
    }
    require (unsplit.wrongFrames == 0, "nonintegral loop never selects a wrong occurrence");
}
inline void linearLogic()
{
    for (bool presentation : { false, true })
    for (int channels : { 1, 2 })
    {
        auto ring = std::make_unique<Ring>(); ring->initialise (pair, rate); ring->header.demand.store (1);
        PreFeeder feeder; TimingObserver observer; Consumer consumer;
        std::array<float, frames> input {}, output {}; input.fill (0.1f);
        const float* in[] { input.data(), input.data() }; float* out[] { output.data(), output.data() };
        int accepted = 0;
        for (int i = 0; i < 80; ++i)
        {
            BlockClock pre; pre.clock = i * frames; pre.project = pre.clock;
            pre.playing = pre.clockValid = pre.projectValid = true; pre.frames = frames;
            pre.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
            pre.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::logicRender);
            pre.presentationSource = 2; pre.outputPresentationValid = presentation;
            feeder.feed (*ring, pre, in, channels);
            auto post = pre;
            if (!presentation) { post.clock += 1024; post.project -= 4096; }
            TimingSnapshot peer; require (readTiming (ring->header.timing, peer), "linear Logic metadata");
            const auto proof = observer.observe (peer, post, rate, ringCapacityFrames);
            consumer.adoptInitialTiming (*ring, post, proof);
            if (i == 60 && presentation) afterCorrespondenceCopyForTest = [] (const Ring& r) {
                const_cast<Ring&> (r).header.timing.sequence.fetch_add (2); };
            const auto result = consumer.process (*ring, pair, rate, post, out, channels);
            afterCorrespondenceCopyForTest = nullptr;
            if (i == 60 && presentation)
                require (result.verdict == Verdict::torn && result.kValid && !result.timelineChanged,
                         "Logic concurrent metadata publication only skips one block");
            if (proof.valid)
            {
                require (qualifiedAudioUnitPair (peer.block, post, proof), "linear Logic analysis retains its qualified coordinate");
                require (proof.kind == (presentation ? LoopEntryKind::logicPresentation : LoopEntryKind::linear), "both linear authorities exercised");
                require (proof.k == (presentation ? 0 : 5120), "independent linear origins are calibrated, not guessed");
            }
            if (result.verdict == Verdict::accepted) ++accepted;
        }
        require (accepted > 40, "Logic linear playback works with and without presentation reports");
    }
}
inline void copyOverlap()
{
    for (int fault = 0; fault < 4; ++fault)
    {
        Host host; host.length = 384000; host.runtimeBound = true;
        for (int i = 0; i < 80; ++i) host.consume();
        if (fault == 0) afterCorrespondenceCopyForTest = [] (const Ring& r) {
            const_cast<Ring&> (r).header.timing.sequence.fetch_add (2); };
        if (fault == 1) afterCorrespondenceCopyForTest = [] (const Ring& r) {
            const_cast<Ring&> (r).header.timing.generation.fetch_add (1); };
        if (fault == 2) afterCorrespondenceCopyForTest = [] (const Ring& r) {
            const_cast<Ring&> (r).header.timing.ownerA.fetch_add (1); };
        if (fault == 3) afterCorrespondenceCopyForTest = [] (const Ring& r) {
            const_cast<Ring&> (r).header.ownerClosed.store (1); };
        host.consume(); afterCorrespondenceCopyForTest = nullptr;
        require (!host.report.preAudible && host.decision.verdict == Verdict::torn, "copy overlap never plays scratch PCM");
        require (host.decision.kValid == (fault == 0), "only sequence overlap retains the calibrated K");
        if (fault == 0) { host.consume(); require (host.report.preAudible, "next coherent block resumes PRE"); }
    }
}
inline void otherHosts()
{
    for (auto basis : { ClockBasis::vst3Continuous, ClockBasis::aaxEngine, ClockBasis::audioUnitRender })
    {
        auto ring = std::make_unique<Ring>(); ring->initialise (pair, rate); ring->header.demand.store (1);
        PreFeeder feeder;
        std::array<float, frames> input {}; const float* in[] { input.data(), input.data() };
        BlockClock block; block.playing = block.clockValid = block.projectValid = true; block.frames = frames;
        block.clockBasis = static_cast<std::uint8_t> (basis);
        block.clockAuthority = static_cast<std::uint8_t> (basis == ClockBasis::aaxEngine
            ? ClockAuthority::boundedAaxEngine : ClockAuthority::certifiedContent);
        for (int i = 0; i < 4; ++i) { block.clock = block.project = i * frames; feeder.feed (*ring, block, in, 2); }
        const auto run = ring->header.run.load(), generation = ring->header.timing.generation.load();
        block.clock += frames; block.project += frames; block.outputPresentationValid = true; block.outputPresentationSamples = 64;
        feeder.feed (*ring, block, in, 2);
        require (ring->header.run.load() == run, "other hosts retain their established PCM run rule");
        require (ring->header.timing.generation.load() != generation, "other hosts still revoke changed timing metadata");
    }
}
}
