#include "../src/live_compare/PairedAnalysisClock.h"
#include "pdc_validation_delay/FixedValidationDelay.h"
#undef NDEBUG
#include <array>
#include <cassert>
#include <iostream>
using namespace hypha::live_compare;

int main()
{
    constexpr int frames = 256, delay = hypha::pdc_validation::FixedValidationDelay::latencySamples;
    for (bool reported : { false, true })
        for (bool includesDelay : { false, true })
            for (bool compensatedProject : { false, true })
            {
                TimingHeader header; header.ownerA.store (7); header.ownerB.store (11);
                TimingPublisher publisher; TimingObserver observer;
                PairedAnalysisClock preAnalysis, postAnalysis;
                hypha::pdc_validation::FixedValidationDelay line; line.prepare (1);
                bool accepted = false;
                BlockClock pre;
                pre.clockValid = pre.projectValid = pre.playing = true;
                pre.frames = frames;
                pre.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
                pre.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedContent);
                pre.presentationSource = 1;
                pre.outputPresentationValid = reported;
                pre.outputPresentationSamples = includesDelay ? delay + 512 : 512;
                auto post = pre;
                post.outputPresentationSamples = 512;
                for (int callback = 0; callback < 80; ++callback)
                {
                    pre.clock = pre.project = callback * frames;
                    post.clock = pre.clock - delay; // certified content coordinate, not an engine tick
                    post.project = compensatedProject ? post.clock : pre.project;
                    std::array<float, frames> pcm {};
                    for (int i = 0; i < frames; ++i) pcm[static_cast<std::size_t> (i)] = static_cast<float> (pre.clock + i);
                    auto* channel = pcm.data(); assert (line.process (&channel, 1, frames));
                    publisher.observe (header, pre, 48000);
                    TimingSnapshot snapshot; assert (readTiming (header, snapshot));
                    const auto evidence = observer.observe (snapshot, post, 48000, 480000);
                    preAnalysis.observeLocal (pre, 48000, true);
                    postAnalysis.observeLocal (post, 48000, true);
                    postAnalysis.observePair (post, evidence, qualifiedVst3Pair (snapshot.block, post, evidence));
                    if (postAnalysis.paired.valid)
                    {
                        accepted = true;
                        assert (compensatedProject && evidence.k == 0);
                        assert (preAnalysis.local.start - postAnalysis.paired.start == delay);
                        for (int i = 0; i < frames; ++i)
                            assert (pcm[static_cast<std::size_t> (i)] == static_cast<float> (postAnalysis.paired.start + i));
                        auto invalid = evidence; invalid.k = 1;
                        assert (! qualifiedVst3Pair (pre, post, invalid));
                        invalid = evidence; --invalid.postClock;
                        assert (! qualifiedVst3Pair (pre, post, invalid));
                        auto unknown = pre; unknown.clockAuthority = 0;
                        assert (! qualifiedVst3Pair (unknown, post, evidence));
                        unknown = pre; unknown.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
                        assert (! qualifiedVst3Pair (unknown, post, evidence));
                    }
                }
                assert (accepted == compensatedProject);
                // Missing proof retires a cached pair, even when its numeric coordinate is unchanged.
                postAnalysis.observePair (post, {}, false);
                assert (! postAnalysis.paired.valid);
                pre.clockValid = false;
                preAnalysis.observeLocal (pre, 48000, true);
                assert (! preAnalysis.local.valid);
            }

    // Initial-loop admission uses the established content-clock certificate and current PRE lap.
    for (bool clamp : { false, true })
      for (int report : { 0, 1, 2 }) // absent, downstream omitted, included
    {
        TimingHeader header; header.ownerA.store (3);
        TimingPublisher publisher; TimingObserver observer;
        PairedAnalysisClock analysis;
        int proven = 0, clamped = 0, gap = 0, longestGap = 0;
        bool wasProven = false;
        constexpr int length = 24000;
        for (int callback = 0; callback < 600; ++callback)
        {
            BlockClock pre;
            pre.clock = callback * frames; pre.project = pre.clock % length;
            pre.frames = frames; pre.playing = pre.clockValid = pre.projectValid = true;
            pre.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
            pre.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedContent);
            pre.presentationSource = 1; pre.outputPresentationValid = report != 0;
            pre.outputPresentationSamples = report == 2 ? delay + 512 : 512;
            pre.loop = { true, true, pre.project / 24000.0, 0.0, 1.0, 120.0 };
            auto post = pre; post.clock -= delay;
            post.outputPresentationSamples = 512;
            post.project = positiveModulo (post.clock, length);
            post.loop.ppq = post.project / 24000.0;
            if (clamp && pre.clock >= length && pre.project < delay)
            {
                post.project = 0;
                post.loop.ppq = (pre.project - delay) / 24000.0;
                ++clamped;
            }
            publisher.observe (header, pre, 48000);
            TimingSnapshot snapshot; assert (readTiming (header, snapshot));
            const auto evidence = observer.observe (snapshot, post, 48000, 480000);
            analysis.observeLocal (post, 48000, true);
            analysis.observePair (post, evidence, qualifiedVst3Pair (snapshot.block, post, evidence));
            if (analysis.paired.valid)
            {
                ++proven; wasProven = true; gap = 0;
                assert (analysis.paired.start == pre.clock - delay);
            }
            else if (wasProven) { ++gap; longestGap = std::max (longestGap, gap); }
        }
        // Without the positive delay witness, the clamped boundary is deliberately not proof.
        // After its tail, ordinary content evidence must return within the calibration streak
        // plus the two boundary observations. This tests time to recover, never false values.
        if (clamp && report != 2) {
            assert (longestGap > 0);
            assert (longestGap <= delay / frames + CalibrationProfile {}.streak + 2);
        } else assert (longestGap == 0);
        std::cout << "content clamp=" << clamp << " report=" << report << " max proof gap=" << longestGap << " blocks\n";
        assert (proven > 400 && (! clamp || clamped > 20));
    }
    std::cout << "Content analysis clocks: latency report matrix, exact delayed PCM, loop/clamp and refusal PASS\n";
}
