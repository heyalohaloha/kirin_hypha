#pragma once
#include "DoublePrecisionParityTest.h"
#include "AnalysisWorkerReceipt.h"
#include "ProductTestClock.h"
#include "DoublePrecisionListenFixture.h"
#include "DoublePrecisionListenDiagnostics.h"
#include <memory>
namespace hypha::tests::precision
{
inline void verifyDoublePrecisionLiveAudition()
{
    ProductTestClock wall;
    struct Clock final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p; p.setIsPlaying (true); p.setTimeInSamples (position);
            p.setKirinAuxiliaryClockSource (1); p.setKirinAuxiliaryClockSamples (position + 960000);
            return p;
        }
        std::int64_t position = 0;
    } host;
    const auto prepare = [&] (Processor::Role role) {
        auto processor = makeDoublePrecisionListenProcessor (role, host);
        prepareFixtureProcessor (*processor, 48000, 256);
        LiveTimingFixtureAccess::enableFixtureWrites (*processor);
        return processor;
    };
    auto pre = prepare (Processor::Role::Pre), post = prepare (Processor::Role::Post);
    DoublePrecisionListenDiagnostics diagnostic;
    bool allowAudition = false, sawMixedFade = false;
    int stableBlocks = 0;
    try
    {
        check (pre->getTotalNumInputChannels() == 2 && pre->getTotalNumOutputChannels() == 2
            && post->getTotalNumInputChannels() == 2 && post->getTotalNumOutputChannels() == 2,
            "double LISTEN buffers match the prepared stereo buses");
        check (LiveTimingFixtureAccess::timingRingPublished (*pre), "double LISTEN requires actual PRE shared memory");
        juce::AudioBuffer<double> upstream (2, 256), output (2, 256);
        juce::MidiBuffer midi;
        // Even samples stay zero in the float view throughout the fade, while retaining a nonzero double A.
        const double a = std::nextafter (0.125, 1.0), tiny = std::ldexp (1.0, -160);
        const auto block = [&] {
            ++diagnostic.callbacks; diagnostic.position = host.position;
            for (int ch = 0; ch < 2; ++ch) for (int f = 0; f < 256; ++f)
            { upstream.setSample (ch, f, f % 2 == 0 ? 0.0 : 0.25); output.setSample (ch, f, f % 2 == 0 ? tiny : a); }
            beginReferenceRtProbe(); pre->processBlock (upstream, midi);
            check (endReferenceRtProbe() == 0, "double PRE feed has no C++ heap operations");
            beginReferenceRtProbe(); post->processBlock (output, midi);
            check (endReferenceRtProbe() == 0, "double LISTEN has no C++ heap operations");
            auto& unchanged = diagnostic.unchanged; auto& changed = diagnostic.changed; auto& fullyPre = diagnostic.fullyPre;
            unchanged = changed = fullyPre = 0;
            for (int ch = 0; ch < 2; ++ch)
            { diagnostic.even[ch] = output.getSample (ch, 0); diagnostic.odd[ch] = output.getSample (ch, 1); }
            for (int ch = 0; ch < 2; ++ch) for (int f = 0; f < 256; ++f)
            {
                const auto sample = output.getSample (ch, f);
                const double original = f % 2 == 0 ? tiny : a;
                if (std::memcmp (&sample, &original, sizeof (double)) == 0) ++unchanged;
                else
                {
                    ++changed;
                    const double widened = static_cast<double> (static_cast<float> (sample));
                    check (allowAudition && std::memcmp (&sample, &widened, sizeof (double)) == 0, "only changed audition samples are widened float PCM");
                    check (std::isfinite (sample), "the known finite PCM sources remain finite during the fade");
                    const double knownPre = 0.25;
                    if (f % 2 != 0 && std::memcmp (&sample, &knownPre, sizeof (double)) == 0) ++fullyPre;
                }
            }
            if (changed > 0 && unchanged > 0 && fullyPre < 256) sawMixedFade = true;
            if (fullyPre == 256 && unchanged == 256) ++stableBlocks; else stableBlocks = 0;
            check (pre->getLatencySamples() == 0 && post->getLatencySamples() == 0, "double LISTEN adds no latency");
            check (LiveTimingFixtureAccess::waitAnalysisInput (*pre, 300000)
                && LiveTimingFixtureAccess::waitAnalysisInput (*post, 300000), "analysis completes actual double input");
            check (LiveTimingFixtureAccess::waitAnalysisExchange (*pre, 300000, true)
                && LiveTimingFixtureAccess::waitAnalysisExchange (*post, 300000, true), "pair publication makes progress");
            host.position += 256; wall.advanceBlock (256, 48000);
            post->serviceLiveCompare();
            return unchanged == 512;
        };
        const auto until = [&] (auto ready, const char* why) {
            for (int blocks = 0; blocks < 48000 * 12 / 256; ++blocks)
            { if (ready()) return; block(); }
            check (ready(), why);
        };
        until ([&] { return post->setPairCandidate (pre->instanceId(), {}); }, "real PRE is published before selecting its pair");
        check (post->startLiveCompare() == live_compare::StartResult::started, "double LISTEN starts through production admission");
        const auto initial = post->liveCompareStatus();
        const float unity = 1.0f;
        check (std::memcmp (&initial.gain, &unity, sizeof (float)) == 0
            && std::memcmp (&initial.postTarget, &unity, sizeof (float)) == 0,
            "the known PRE PCM expectation uses the admitted unity gains without MATCH");
        post->selectLiveComparePre (true); allowAudition = true;
        until ([&] {
            const auto state = post->liveCompareStatus();
            return stableBlocks >= 4 && state.preSelected && state.preAudible
                && state.verdict == live_compare::Verdict::accepted;
        }, "double LISTEN renders known PRE PCM and preserves unchanged A samples");
        check (sawMixedFade, "the actual renderer exercised a mixed fade block");
        post->finishLiveCompare();
        int restored = 0;
        for (int blocks = 0; blocks < 48000 / 256 && restored < 4; ++blocks) if (block()) ++restored; else restored = 0;
        check (restored >= 4, "END restores exact double A after the fade");
        pre->releaseResources(); post->releaseResources();
        std::cout << "double LISTEN: actual PRE PCM, mixed fade, unchanged low bits, END, RT heap PASS\n";
    }
    catch (...) { diagnostic.print (*pre, *post, stableBlocks, sawMixedFade); throw; }
}
}
