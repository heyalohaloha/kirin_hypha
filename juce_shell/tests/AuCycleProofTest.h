#pragma once
#include "../src/live_compare/LiveCompareLoopGuidance.h"
#include "../src/live_compare/LiveCompareSession.h"
#include "../src/live_compare/LiveBlindSession.h"
#include "../src/live_compare/LiveBlindPreparation.h"
#include <cstring>
#include <stdexcept>

inline void verifyAuCycleProof()
{
    using namespace hypha::live_compare;
    const auto require = [] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); };
    constexpr int frames = 256, length = 24000, delay = 4096;
    constexpr std::uint32_t rate = 48000;
    constexpr std::uint64_t key = 91;
    int cases = 0;
    for (int channels : { 1, 2 })
        for (int fault : { 0, 1, 2, 3, 4 }) // missing, zero, equal, missing cycle, proven
        {
            auto ring = std::make_unique<Ring>(); ring->initialise (key, rate);
            ring->header.demand.store (1);
            PreFeeder feeder; TimingObserver observer; PairedAnalysisClock analysis;
            PostRenderer renderer; renderer.prepare (frames, rate);
            PostLevel level; level.configure (rate);
            hypha::pdc_validation::FixedValidationDelay line; line.prepare (channels);
            std::array<std::array<float, frames>, 2> source {}, output {};
            int acceptedCycle = 0, acceptedLinear = 0, gap = 0, longestGap = 0;
            BlindSession continuousBlind; BlindPreparation continuousPreparation;
            continuousPreparation.begin (1);
            for (int callback = 0; callback < 650; ++callback)
            {
                const bool cycle = callback < 550;
                const auto time = callback * frames;
                BlockClock pre;
                pre.clock = time; pre.project = cycle ? time % length : time;
                pre.frames = frames; pre.playing = pre.projectValid = pre.clockValid = true;
                pre.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
                pre.presentationSource = 2; pre.outputPresentationValid = fault != 0;
                pre.outputPresentationSamples = fault == 1 ? 0 : fault == 2 ? 512 : delay + 512;
                pre.loop = { cycle, fault != 3, pre.project / 24000.0, 0, 1, 120 };
                auto post = pre;
                post.project = cycle ? positiveModulo (time - delay, length) : time - delay;
                post.loop.ppq = post.project / 24000.0;
                post.outputPresentationSamples = fault == 1 ? 0 : 512;
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < frames; ++i)
                        source[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)]
                            = static_cast<float> ((time + i + ch * 7) % 8191) / 32768.0f;
                output = source;
                float* io[] { output[0].data(), output[1].data() };
                require (line.process (io, channels, frames), "physical delay is applied once");
                const auto expected = output;
                const float* input[] { source[0].data(), source[1].data() };
                feeder.feed (*ring, pre, input, channels);
                const bool unavailableWitness = fault == 4 && callback > 150
                    && (callback == 500 || (cycle && post.project < frames));
                if (unavailableWitness) ring->header.timing.sequence.fetch_add (1);
                TimingSnapshot snapshot;
                require (readTiming (ring->header.timing, snapshot) != unavailableWitness,
                         "metadata overlap is a torn read, not a coherent callback witness");
                const auto evidence = unavailableWitness ? observer.unavailable (post, rate, ringCapacityFrames)
                    : observer.observe (snapshot, post, rate, ringCapacityFrames);
                const bool qualified = qualifiedAudioUnitPair (snapshot.block, post, evidence);
                analysis.observeLocal (post, rate, true);
                analysis.observePair (post, evidence, qualified);
                const auto block = pairingLoopBlock (post, true, false, evidence, true);
                if (cycle && fault != 4)
                    require (! analysis.paired.valid && block == LoopPairingBlock::auCycle,
                             "unreported / zero / equal latency or unknown cycle has no delta and gives Cycle guidance");
                if (! cycle || qualified)
                    require (block == LoopPairingBlock::none, "linear and proven Cycle never request Cycle off");
                renderer.adoptInitialTiming (*ring, post, evidence);
                const auto report = renderer.render (*ring, key, rate, post, io, channels, true,
                    1.0f, level, 1.0f, 1.0f, false,
                    block == LoopPairingBlock::auCycle ? RecoveryReason::cycleUnverified : RecoveryReason::none);
                if (unavailableWitness) ring->header.timing.sequence.fetch_add (1);
                if (cycle && fault == 4 && acceptedCycle > 0)
                {
                    if (! continuousBlind.command().active()) continuousBlind.start (true);
                    if (report.verdict != Verdict::accepted)
                        continuousBlind.invalidate (continuousBlind.command(), report.reason);
                    continuousPreparation.observe (continuousPreparation.command(),
                        report.verdict == Verdict::accepted, report.reason, 1);
                    require (! continuousBlind.view().invalidated
                        && continuousPreparation.failure() == RecoveryReason::none,
                        "a proven Cycle retains BLIND through metadata overlap and wrap observations");
                    require (report.preAudible && output == expected,
                             "LISTEN retains the exact PRE PCM through metadata overlap and wraps");
                }
                if (block == LoopPairingBlock::auCycle && fault != 4)
                {
                    require (! analysis.paired.valid, "the same callback withholds delta as well as PRE");
                    require (! report.preAudible && report.preWaiting && report.reason == RecoveryReason::cycleUnverified,
                             "unverified Cycle keeps named PRE selected and reports its reason");
                    require (output == expected, "unverified Cycle outputs exactly POST, with no PRE fade");
                }
                if (report.preAudible)
                {
                    if (unavailableWitness)
                        require (! analysis.paired.valid, "torn metadata cannot authorise a new delta");
                    else
                        require (analysis.paired.valid, "coherent fixture evidence still authorises its exact delta");
                    if (report.stableSource)
                        require (output == expected, "proven PRE is the exact physical-delay PCM, including its lap");
                    if (cycle) ++acceptedCycle; else ++acceptedLinear;
                    gap = 0;
                }
                else if (cycle && acceptedCycle > 0) longestGap = std::max (longestGap, ++gap);
            }
            std::cout << "AU Cycle channels=" << channels << " fault=" << fault
                      << " accepted=" << acceptedCycle << " longestGap=" << longestGap << "\n";
            require (fault == 4 ? acceptedCycle > 200 : acceptedCycle == 0, "only proven initial cycles audition PRE");
            // The PCM proof survives every unclamped wrap, including clock-only writer overlap.
            // No post-admission silent callback is needed (0 blocks, not an aggregate quota).
            require (longestGap == 0, "established AU Cycle PCM has no proof gap");
            require (acceptedLinear > 50, "Cycle off resumes through fresh linear proof");
            // Blind retains the cause even after the current blocker clears.
            BlindSession blind; blind.start (true);
            BlindPreparation preparation; preparation.begin (1);
            preparation.observe (preparation.command(), true, RecoveryReason::none, 1);
            blind.invalidate (blind.command(), RecoveryReason::cycleUnverified);
            preparation.observe (preparation.command(), false, RecoveryReason::cycleUnverified, 1);
            blind.invalidate (blind.command(), RecoveryReason::stopped);
            preparation.observe (preparation.command(), true, RecoveryReason::none, 1);
            require (blind.view().reason == RecoveryReason::cycleUnverified && blind.view().invalidated
                && preparation.failure() == RecoveryReason::cycleUnverified, "Cycle interruption cannot auto-restart Blind");
            blind.reset(); preparation.end();
            require (! blind.view().invalidated, "explicit END clears the old trial");
            ++cases;
        }
    BlockClock post; post.playing = post.loop.active = true;
    require (pairingLoopBlock (post, false, false, {}, true) == LoopPairingBlock::none,
             "VST3 host clocks never acquire AU Cycle guidance");
    require (pairingLoopBlock (post, false, true, {}, true) == LoopPairingBlock::aaxLoop,
             "uncertified AAX keeps Loop Playback guidance");
    require (pairingLoopBlock (post, true, false, {}, false) == LoopPairingBlock::none,
             "bypass / offline / other output owner are not Cycle proof failures");
    // The renderer already supports a linear proof carried into a mixed VST3/AU cycle. The
    // analysis format filter is narrower; it must not turn that valid audition into Cycle WAIT.
    TimingHeader mixedHeader; mixedHeader.ownerA.store (1);
    TimingPublisher mixedPublisher; TimingObserver mixedObserver;
    int mixedCycles = 0;
    for (int callback = 0; callback < 250; ++callback)
    {
        const auto time = callback * frames;
        const bool cycle = callback >= 50;
        BlockClock pre;
        pre.clock = time; pre.project = cycle ? time % length : time;
        pre.frames = frames; pre.playing = pre.projectValid = pre.clockValid = true;
        pre.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
        pre.loop = { cycle, cycle, pre.project / 24000.0, 0, 1, 120 };
        post = pre; post.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
        post.project = cycle ? positiveModulo (time - delay, length) : time - delay;
        post.loop.ppq = post.project / 24000.0;
        mixedPublisher.observe (mixedHeader, pre, rate);
        TimingSnapshot snapshot; require (readTiming (mixedHeader, snapshot), "mixed callback witness");
        const auto evidence = mixedObserver.observe (snapshot, post, rate, ringCapacityFrames);
        if (! cycle || ! evidence.valid) continue;
        require (! qualifiedAudioUnitPair (snapshot.block, post, evidence), "mixed analysis is a distinct boundary");
        require (pairingLoopBlock (post, true, false, evidence, true)
            == LoopPairingBlock::none, "proven mixed-format cycles retain their existing audition");
        ++mixedCycles;
    }
    require (mixedCycles > 150, "mixed linear proof remains valid through repeated cycles");
    std::cout << "AU Cycle proof: " << cases << " mono/stereo report/cycle cases, PCM and linear recovery PASS\n";
}
