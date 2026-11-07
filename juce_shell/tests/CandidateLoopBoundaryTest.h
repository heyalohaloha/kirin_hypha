#pragma once
#include "CandidateProductHost.h"
#include "CandidateChainHost.h"

namespace hypha::tests
{
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
