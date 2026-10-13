#include "ReaperFixture.h"
#include "LoopReviewRegressions.h"
#include "../ProductTestClock.h"
#include "../../src/live_compare/PairedAnalysisClock.h"
using namespace reaper_fixture;
int main()
{
    loop_review::linearLogic();
    hypha::tests::ProductTestClock time;
    for (int blockFrames : { 512, 2048 })
    for (int channels : { 1, 2 })
    for (int delay : { 0, 4096 })
    {
        auto ring = std::make_unique<Ring>(); ring->initialise (pair, rate); ring->header.demand.store (1);
        PreFeeder feeder; TimingObserver observer; PairedAnalysisClock analysis; PostRenderer renderer; PostLevel level;
        renderer.prepare (blockFrames, rate); level.configure (rate);
        BlindSession blind; NamedSelection selection; selection.select (true); NamedReentry reentry;
        std::array<std::array<float, 2048>, 2> input {}, output {};
        const float* in[] { input[0].data(), input[1].data() };
        float* out[] { output[0].data(), output[1].data() };
        const auto fillPost = [&] { for (auto& channel : output) channel.fill (-0.25f); };
        auto block = [&] (std::int64_t render, bool pre)
        {
            BlockClock b;
            b.clock = render; b.project = positiveModulo (render + (render >= blockFrames ? 1 : 0), 384123);
            b.clockValid = b.projectValid = b.playing = true; b.frames = blockFrames;
            b.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
            b.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::logicRender);
            b.presentationSource = 2; b.outputPresentationValid = true;
            b.outputPresentationSamples = static_cast<std::uint32_t> (685 + (pre ? delay : 0));
            b.loop = { true, true, b.project / 48000.0, 0, 384123.0 / 48000.0, 120 };
            b.wallNanos = time.nanos(); return b;
        };
        auto publish = [&] (std::int64_t render)
        {
            for (int c = 0; c < channels; ++c) for (int i = 0; i < blockFrames; ++i)
                input[c][i] = Host::sample (render + i, c);
            feeder.feed (*ring, block (render, true), in, channels);
        };
        // Preparation burst contains the delayed prefix; first live PRE callback is D ahead.
        for (int i = 0; i < delay; i += blockFrames) publish (i);
        int accepted = 0;
        for (int i = 0; i < 1600; ++i)
        {
            const auto render = static_cast<std::int64_t> (i) * blockFrames;
            publish (render + delay);
            auto post = block (render, false);
            TimingSnapshot peer; require (readTiming (ring->header.timing, peer), "Logic metadata");
            const auto proof = observer.observe (peer, post, rate, ringCapacityFrames);
            require (proof.valid && proof.k == 0, "Logic render plus presentation proves this callback");
            require (qualifiedAudioUnitPair (peer.block, post, proof), "same callback qualifies delta");
            analysis.observeLocal (post, rate, true);
            analysis.observePair (post, proof, qualifiedAudioUnitPair (peer.block, post, proof));
            require (analysis.local.valid && analysis.paired.valid && analysis.paired.start == render,
                     "analysis receives continuous content coordinates through every project wrap");
            renderer.adoptInitialTiming (*ring, post, proof);
            fillPost();
            if (i == 300) blind.start (true);
            const auto report = renderer.render (*ring, pair, rate, post, out, channels, true,
                1.0f, level, 1.0f, 1.0f, blind.command().active());
            require (report.verdict == Verdict::accepted, "cycle and one-sample project jump preserve output");
            if (report.stableSource && report.preAudible)
            {
                ++accepted;
                for (int c = 0; c < channels; ++c) for (int n = 0; n < blockFrames; ++n)
                    require (output[c][n] == Host::sample (render + n, c), "Logic correct PRE occurrence");
            }
            time.advanceBlock (blockFrames, rate);
        }
        require (accepted > 1500, "Logic actually plays PRE");
        // Independent callback proof: one side loses/changes the report, or disagrees about
        // render position. No modulo project match may hide it, including delay zero.
        std::int64_t nextBlock = 1600;
        for (int fault = 0; fault < 5; ++fault)
        {
            if (fault != 0) { blind.end(); blind.start (true); }
            require (blind.valid (blind.command()), "each fault interrupts a fresh active trial");
            const auto render = nextBlock++ * blockFrames;
            publish (render + delay);
            auto post = block (render, false);
            if (fault == 0) post.outputPresentationValid = false;
            if (fault == 1) ++post.outputPresentationSamples;
            if (fault == 2) ++post.clock;
            if (fault == 3) post.clockAuthority = 0;
            if (fault == 4) post.afterGap = true;
            TimingSnapshot peer; require (readTiming (ring->header.timing, peer), "fault metadata");
            require (!logicRenderPair (peer.block, post), "each callback fault independently loses the proof");
            const auto proof = observer.observe (peer, post, rate, ringCapacityFrames);
            require (!proof.valid || !qualifiedAudioUnitPair (peer.block, post, proof), "unverified delta withheld");
            fillPost();
            const auto command = blind.command();
            const auto report = renderer.render (*ring, pair, rate, post, out, channels,
                blind.valid (command), 1.0f, level, 1.0f, 1.0f, command.active());
            require (report.verdict != Verdict::accepted, "every injected fault rejects this block");
            if (report.verdict != Verdict::accepted) blind.fallback (command);
            if (report.timelineChanged) reentry.lost (report.loss, true);
            require (!blind.view().invalidated && !report.preAudible, "unverified Logic callback keeps POST and the same BLIND");
            for (const auto& channel : output) for (auto sample : channel) require (sample == -0.25f, "fault block keeps original POST");
            time.advanceBlock (blockFrames, rate);
            for (int recovery = 0; recovery < 45; ++recovery)
            {
                const auto restoredRender = nextBlock++ * blockFrames;
                publish (restoredRender + delay); auto restored = block (restoredRender, false);
                TimingSnapshot restoredPeer; require (readTiming (ring->header.timing, restoredPeer), "recovery metadata");
                const auto restoredProof = observer.observe (restoredPeer, restored, rate, ringCapacityFrames);
                if (reentry.request (selection.command(), true, renderer.needsNewAdmission())) renderer.renewNamedTiming();
                renderer.adoptInitialTiming (*ring, restored, restoredProof); fillPost();
                const auto restoredReport = renderer.render (*ring, pair, rate, restored, out, channels,
                    blind.valid (blind.command()), 1.0f, level, 1.0f, 1.0f, true);
                if (restoredReport.timelineChanged) reentry.lost (restoredReport.loss, true);
                require (!blind.view().invalidated, "Logic preserves the same trial");
                if (recovery > 20) require (restoredReport.preAudible, "Logic resumes on fresh proof without another gesture");
                if (restoredReport.stableSource && restoredReport.preAudible)
                    for (int c = 0; c < channels; ++c) for (int n = 0; n < blockFrames; ++n)
                        require (output[c][n] == Host::sample (restoredRender + n, c), "recovered PRE has the correct occurrence");
                time.advanceBlock (blockFrames, rate);
            }
        }
    }
    std::puts ("Logic render/presentation cycle output and delta PASS");
}
