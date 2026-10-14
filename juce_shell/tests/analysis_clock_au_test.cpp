#include "../src/live_compare/PairedAnalysisClock.h"
#include "pdc_validation_delay/FixedValidationDelay.h"
#undef NDEBUG
#include <array>
#include <cassert>
#include <iostream>
#include "AuCycleProofTest.h"
using namespace hypha::live_compare;

int main()
{
    verifyAuCycleProof();
    constexpr int frames = 256, delay = hypha::pdc_validation::FixedValidationDelay::latencySamples;
    int cases = 0;
    // Independent AU render origins: K is a coordinate translation, not necessarily the delay.
    for (int origin : { -8192, 0, 8192 })
        for (int report : { 0, 1, 2, 3 }) // absent, downstream omitted, included, explicit zero
            for (int mode : { 0, 1, 2 }) // linear, initial loop, linear then loop
            {
                TimingHeader header; header.ownerA.store (7); header.ownerB.store (11);
                TimingPublisher publisher; TimingObserver observer;
                PairedAnalysisClock preAnalysis, postAnalysis;
                hypha::pdc_validation::FixedValidationDelay line; line.prepare (1);
                int accepted = 0, acceptedLoop = 0, gap = 0, longestGap = 0;
                constexpr int length = 24000;
                for (int callback = 0; callback < 700; ++callback)
                {
                    const auto t = callback * frames;
                    const bool loop = mode == 1 || (mode == 2 && callback >= 50);
                    BlockClock pre;
                    pre.clock = t; pre.project = loop ? t % length : t;
                    pre.clockValid = pre.projectValid = pre.playing = true;
                    pre.frames = frames;
                    pre.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
                    pre.presentationSource = 2; pre.outputPresentationValid = report != 0;
                    pre.outputPresentationSamples = report == 2 ? delay + 512 : report == 3 ? 0 : 512;
                    pre.loop = { loop, loop, pre.project / 24000.0, 0.0, 1.0, 120.0 };
                    auto post = pre;
                    post.clock = t + origin;
                    post.project = loop ? positiveModulo (t - delay, length) : t - delay;
                    post.loop.ppq = post.project / 24000.0;
                    post.outputPresentationSamples = report == 3 ? 0 : 512;
                    std::array<float, frames> pcm {};
                    for (int i = 0; i < frames; ++i) pcm[static_cast<std::size_t> (i)] = static_cast<float> (t + i);
                    auto* channel = pcm.data(); assert (line.process (&channel, 1, frames));
                    publisher.observe (header, pre, 48000);
                    TimingSnapshot snapshot; assert (readTiming (header, snapshot));
                    const auto evidence = observer.observe (snapshot, post, 48000, 480000);
                    preAnalysis.observeLocal (pre, 48000, true);
                    postAnalysis.observeLocal (post, 48000, true);
                    if (callback > 1 && !loop) assert (preAnalysis.local.valid && postAnalysis.local.valid);
                    const auto qualified = qualifiedAudioUnitPair (snapshot.block, post, evidence);
                    postAnalysis.observePair (post, evidence, qualified);
                    if (postAnalysis.paired.valid)
                    {
                        ++accepted; if (loop) ++acceptedLoop;
                        gap = 0;
                        assert (evidence.k == delay + origin);
                        assert (postAnalysis.paired.start == t - delay);
                        for (int i = 0; i < frames; ++i)
                            assert (pcm[static_cast<std::size_t> (i)] == static_cast<float> (postAnalysis.paired.start + i));
                        auto bad = evidence; --bad.postClock;
                        assert (! qualifiedAudioUnitPair (snapshot.block, post, bad));
                        bad = evidence; bad.preGeneration = 0;
                        assert (! qualifiedAudioUnitPair (snapshot.block, post, bad));
                        bad = evidence; bad.ownerA = bad.ownerB = 0;
                        assert (! qualifiedAudioUnitPair (snapshot.block, post, bad));
                        bad = evidence; bad.kind = LoopEntryKind::contentClock;
                        assert (! qualifiedAudioUnitPair (snapshot.block, post, bad));
                        auto wrong = snapshot.block; wrong.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
                        assert (! qualifiedAudioUnitPair (wrong, post, evidence));
                    }
                    else if (accepted > 0) longestGap = std::max (longestGap, ++gap);
                    postAnalysis.observePair (post, {}, false);
                    assert (! postAnalysis.paired.valid);
                    auto missing = post; missing.clockValid = false;
                    auto lost = postAnalysis;
                    lost.observeLocal (missing, 48000, true);
                    assert (! lost.local.valid);
                    assert (! qualifiedAudioUnitPair (snapshot.block, missing, evidence));
                }
                std::cerr << "origin=" << origin << " report=" << report << " mode=" << mode
                          << " accepted=" << accepted << " loop=" << acceptedLoop << " longestGap=" << longestGap << "\n";
                if (mode == 1 && report != 2) assert (accepted == 0);
                else assert (accepted > 100);
                if (mode == 2 || (mode == 1 && report == 2)) assert (acceptedLoop > 100);
                // Exact continuous render clocks and unclamped project positions retain K at
                // every wrap. After admission no callback needs recalibration: recovery is
                // immediate (0 blocks), independent of the initial calibration/loop wait.
                assert (longestGap == 0);
                ++cases;
            }
    std::cout << "AU analysis clock: " << cases << " delay/report/origin/playback cases PASS\n";
}
