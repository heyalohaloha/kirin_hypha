#pragma once
#include "CandidateProductHost.h"
#include "CandidateChainHost.h"
#include "../src/HyphaLiveCompareRecoveryText.h"

namespace hypha::tests
{
// Production processor entry points and independent physical delays: project wraps while the
// compensated continuous clocks keep advancing. The repeated signal is deliberately unable
// to distinguish laps; only the current clock/presentation proof may grant the read.
template<class Require> void verifyCubaseCycleProduct (Require require)
{
   #if JUCE_MAC
    using namespace live_compare;
    struct SignalScope
    {
        bool broadband = candidateBroadband;
        std::int64_t period = candidateInputPeriod;
        ~SignalScope() { candidateBroadband = broadband; candidateInputPeriod = period; }
    } restore;
    candidateBroadband = true;
    for (const int delay : { 0, 96, 4096 })
    for (const bool repeated : { false, true })
    for (const bool candidate : { false, true })
    {
        constexpr int frames = 512;
        const std::int64_t length = repeated ? 24000 : 768000;
        candidateInputPeriod = repeated ? length : 0;
        ProductTestClock wall;
        CandidateHost<Require> host (wall, require, repeated ? 2 : 1, frames);
        host.delayA = delay / 4; host.delayB = delay - host.delayA;
        for (std::size_t side = 0; side < host.instances.size(); ++side)
        {
            require (LiveTimingFixtureAccess::configureVst3Clock (*host.instances[side], "Cubase 16", "16.0.5.174"),
                     "Cubase cycle uses the native executable/version policy, never an injected certificate");
            auto& clock = host.clocks[side];
            clock.compensatedContent = true;
            clock.outputPresentation = 256 + (side == 0 ? delay : side == 1 ? host.delayB : 0);
        }
        host.pair(); auto& post = host.post();
        for (auto& clock : host.clocks)
        { clock.offset = clock.loopStart = 64 * 24000; clock.loopLength = length; }
        require ((candidate ? post.startCandidateCompare (host.pre2().instanceId(), true) : post.startLiveCompare())
                    == StartResult::started, "Cubase cycle starts through the real LISTEN or A/B entry");
        post.selectLiveComparePre (true);
        host.wholeOutput = candidate;
        CandidateOutputOracle listenOutput;
        const auto checkOutput = [&] {
            if (candidate) return; // wholeOutput already checks A/B, including every transition.
            const auto status = post.liveCompareStatus();
            // LISTEN loses proof at the block start; only A/B has a scalar return fade.
            if (status.verdict != Verdict::accepted)
            { listenOutput.sourceWeight = 0.0f; listenOutput.transitionFrame = 240; }
            listenOutput.check (require, host.audio, status.verdict == Verdict::accepted, true,
                [&] (int i, int ch) { return candidateInput (host.cursor - frames + i - delay, ch); },
                [&] (int i, int ch) {
                    const auto at = host.cursor - frames + i - host.delayB;
                    return candidateB (host.history (ch, at), host.history (ch, at - 997));
                });
        };
        const auto ready = [&] (int blocks)
        {
            for (int n = 0; n < blocks; ++n)
            {
                host.render (1, candidate, true, false);
                checkOutput(); // POST preparation, the full fade and the first PRE sample.
                const auto status = post.liveCompareStatus();
                require (status.preSelected && (n < 32 || (status.verdict == Verdict::accepted && status.preAudible)),
                         "Cubase initial cycle, wraps and fresh reentry automatically play the selected source");
            }
        };
        const auto rejected = [&] (int blocks, int provenTail = 0)
        {
            for (int n = 0; n < blocks; ++n)
            {
                host.render (1, candidate, true, false);
                checkOutput();
                const auto status = post.liveCompareStatus();
                // PRE1 loss reaches POST only after B's physical delay. Its preceding,
                // independently receipted PRE2 ranges still describe valid B input.
                if (n < provenTail) continue; // wholeOutput audits every such callback as usual.
                if (status.verdict == Verdict::accepted || !status.preSelected)
                    std::cerr << "Cubase rejection: delay=" << delay << " repeated=" << repeated
                              << " candidate=" << candidate << " block=" << n
                              << " provenTail=" << provenTail << " reason=" << static_cast<int> (status.reason) << '\n';
                require (status.verdict != Verdict::accepted && status.preSelected,
                         "contradictory or missing cycle proof keeps the selection but never grants PRE/A");
                if (candidate && n == provenTail) continue; // A/B's scalar return fits inside this first block.
                for (int ch = 0; ch < host.channels; ++ch) for (int i = 0; i < frames; ++i)
                {
                    const auto at = host.cursor - frames + i - host.delayB;
                    const auto expected = candidateB (host.history (ch, at), host.history (ch, at - 997));
                    require (std::abs (host.audio.getSample (ch, i) - expected) < 4.0e-6f,
                             "unverified cycle output is the actual POST/B, never old PRE PCM");
                }
            }
        };
        const auto clockNotice = [&] {
            rejected (320); // More than three seconds of real processor callback time.
            const auto status = post.liveCompareStatus();
            if (!status.prolongedWait || status.observation != RecoveryReason::loopClockUnavailable)
                std::cerr << "Cubase clock notice: delay=" << delay << " candidate=" << candidate
                          << " prolonged=" << status.prolongedWait << " observation=" << static_cast<int> (status.observation)
                          << " verdict=" << static_cast<int> (status.verdict) << '\n';
            require (status.prolongedWait && status.observation == RecoveryReason::loopClockUnavailable,
                     "certified Cubase missing or contradictory presentation is unavailable, not another first pass");
            require (juce::String (live_compare_ui::namedRecovery (status))
                         == "Loop timing unavailable; turn loop off to compare (POST)",
                     "the prolonged Cubase wait uses the existing unavailable-clock remedy");
        };
        ready (static_cast<int> (2 * length / frames) + 64);
        // The loop-only content proof is K=0, not a modulo delay. Either side shifted by a
        // whole lap must remain rejected even when every sample of adjacent laps is identical.
        for (const int side : { 0, 2 })
        for (const int direction : { -1, 1 })
        {
            host.clocks[side].continuousOffset += direction * length;
            rejected (16, candidate && side == 0 ? (host.delayB + frames - 1) / frames : 0);
            host.clocks[side].continuousOffset -= direction * length;
            ready (64);
        }
        host.clocks[2].outputPresentation += 96;
        clockNotice(); // presentation PDC disagrees with otherwise coherent clock positions
        host.clocks[2].outputPresentation -= 96;
        ready (64);
        host.reportedDelayB += 96;
        clockNotice(); // native/continuous PDC changes without the presentation witness
        host.reportedDelayB -= 96;
        ready (64);
        host.clocks[2].presentationAvailable = false;
        clockNotice(); host.clocks[2].presentationAvailable = true; ready (64);
        host.clocks[2].auxiliaryAvailable = false;
        rejected (16); host.clocks[2].auxiliaryAvailable = true; ready (64);
        // A seek ends the old run. Fresh demanded PCM must cover the new address before PRE
        // returns; keeping the selected button cannot make the old generation audible.
        for (auto& clock : host.clocks) clock.offset += 7000;
        rejected (1); ready (96);
        const auto gain = post.liveCompareStatus().gain;
        require (gain == 1.0f && post.getLatencySamples() == 0, "cycle recovery keeps gain and zero added latency");
        host.wholeOutput = false;
        post.finishLiveCompare(); host.render (20, candidate, false, false);
        std::cout << "Cubase cycle product: " << (candidate ? "A/B" : "LISTEN") << " delay=" << delay
                  << " repeated=" << repeated << " PASS\n" << std::flush;
    }
   #else
    (void) require;
   #endif
}

// A hardware insert: serial PDC far beyond one callback, loops inside the song, and the live chain
// values REAPER reports for it. The chain value is the fill of its delay buffer in whole callbacks.
// One callback split at PRE's wrap lowers it while that short entry stays inside. A seek rebuilds
// the buffer: every FX reads 0 for a moment, and the fill climbs back.
struct LoopRoute { int frames, delayB, chain; bool hardware; };
inline void liveChainAfterWrap (CandidateChainHost& api, const LoopRoute& route, int blocksSinceWrap, bool reportDips)
{
    const auto dipped = blocksSinceWrap >= 0 && blocksSinceWrap < route.chain / route.frames;
    api.fill = dipped ? route.chain - route.frames / 2 : -1;
    api.reported = dipped && reportDips ? route.chain - route.frames / 2 : -1;
}
inline void liveChainAfterSeek (CandidateChainHost& api, const LoopRoute& route, int blocksSinceSeek)
{
    constexpr int rebuild = 12;
    const auto fill = (blocksSinceSeek - rebuild) * 1024;
    api.fill = blocksSinceSeek < rebuild ? 0 : fill < route.chain ? fill : -1;
    api.reported = blocksSinceSeek < rebuild ? 0 : -1;
}
template<class Require> void verifyCandidateLoopBoundary (Require require)
{
    using namespace live_compare;
    verifyCubaseCycleProduct (require);
    for (const auto& route : { LoopRoute { 256, 383, 510, false }, { 512, 383, 510, false }, { 1024, 383, 510, false },
                               { 512, 6217, 6656, true } })
        for (const int seconds : { 21, 45 })
            for (int mode = route.hardware && seconds != 21 ? 5 : 0; mode < 5; ++mode)
            {
                const auto frames = route.frames;
                ProductTestClock wall;
                CandidateChainHost api; api.delay = route.chain;
                CandidateHost<Require> host (wall, require, 2, frames);
                host.delayB = route.delayB;
                if (route.hardware) host.timerBlocks = 5; // the 50 ms service timer
                for (auto& processor : host.instances)
                {
                    require (LiveTimingFixtureAccess::configureReaperClock (*processor), "loop fixture uses REAPER adapter");
                    processor->getVST3ClientExtensions()->setIHostApplication (&api);
                }
                host.processorTimers = true;
                host.render (2 * route.chain / frames, false, false); // POST's position must pass the song start
                host.pair(); auto& post = host.post();
                const auto length = static_cast<std::int64_t> (seconds) * 48000;
                for (auto& clock : host.clocks) clock.loopLength = length;
                const auto exactPre = [&]
                {
                    bool exact = true;
                    for (int ch = 0; ch < host.channels; ++ch) for (int i = 0; i < frames; ++i)
                        exact = exact && std::abs (host.audio.getSample (ch, i) - candidateInput (
                            host.cursor - frames + i - host.delayA - host.delayB, ch)) < 3.0e-6f;
                    return exact;
                };
                const bool ordinary = mode == 0, blind = mode >= 3;
                require ((ordinary ? post.startLiveCompare() : post.startCandidateCompare (host.pre2().instanceId(), true))
                    == StartResult::started, "loop fixture starts through processor entry");
                post.selectLiveComparePre (mode != 2);
                host.render (1100, !ordinary, mode != 2, false);
                if (blind)
                {
                    host.initialBlindSourceA = mode == 3;
                    require (post.beginLiveBlind() == StartResult::started, "loop fixture starts BLIND");
                    host.render (1000, true, false, false);
                    require (post.liveBlindStatus().stage == BlindStage::active, "loop fixture reaches BLIND playback");
                }
                const auto command = LiveTimingFixtureAccess::command (post);
                const auto gain = post.liveCompareStatus().gain;
                if (!ordinary) { host.sourceContinuity.begin(); host.outputContinuity.begin(); }
                bool arrived = false;
                const auto duration = 2 * seconds * 48000 / frames + 4;
                int blocksSinceWrap = -1, wraps = 0;
                for (int n = 0; n < duration; ++n)
                {
                    if (route.hardware)
                    {
                        if (((host.cursor + host.clocks[0].offset) % length + length) % length < frames)
                        { blocksSinceWrap = 0; ++wraps; }
                        liveChainAfterWrap (api, route, blocksSinceWrap, wraps % 2 == 0);
                        if (blocksSinceWrap >= 0) ++blocksSinceWrap;
                    }
                    host.render (1, !ordinary, mode == 1, false);
                    if (ordinary)
                    {
                        const bool exact = exactPre();
                        require (!arrived || exact, "ordinary PRE remains exact through every loop boundary");
                        arrived = arrived || exact;
                        require (n < 32 || arrived, "ordinary PRE reaches the independent sample oracle");
                    }
                }
                host.sourceContinuity.enabled = host.outputContinuity.enabled = false;
                // The whole-path oracles start here, so they begin from the sound before the seek.
                const bool sourceA = blind ? command.pre() : mode == 1;
                host.outputOracle = {}; host.outputOracle.sourceWeight = sourceA ? 1.0f : 0.0f;
                host.receiverOracle.previouslyExact = !ordinary;
                for (int ch = 0; ch < host.channels; ++ch)
                {
                    host.outputOracle.previous[static_cast<std::size_t> (ch)] = host.audio.getSample (ch, frames - 1);
                    host.receiverOracle.last[static_cast<std::size_t> (ch)] = host.history (ch, host.cursor - 1);
                }
                for (auto& clock : host.clocks) clock.offset -= 7000;
                host.wholeOutput = !ordinary;
                if (route.hardware)
                {
                    // The chain keeps its K: PRE returns once the delay itself has passed.
                    const auto settled = (host.delayA + host.delayB) / frames + 3;
                    bool returned = false;
                    for (int n = 0; n < 2 * settled; ++n)
                    {
                        liveChainAfterSeek (api, route, n);
                        host.render (1, !ordinary, mode == 1, false);
                        returned = returned || (ordinary && n < settled && exactPre());
                        require (!ordinary || !returned || exactPre(), "PRE stays exact once it has returned");
                    }
                    require (!ordinary || returned, "PRE returns right after the delay of a seek");
                    api.fill = api.reported = -1;
                }
                host.render (100, !ordinary, mode == 1, false);
                require (post.liveCompareStatus().gain == gain, "seek retains the approved gain");
                if (blind)
                {
                    const auto next = LiveTimingFixtureAccess::command (post);
                    require (next.epoch() == command.epoch() && next.stimulus() == command.stimulus()
                        && next.firstPre() == command.firstPre(), "seek retains anonymous selection and epoch");
                }
                for (auto& processor : host.instances) processor->getVST3ClientExtensions()->setIHostApplication (nullptr);
            }
}
}
