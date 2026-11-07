#pragma once
#include "CandidateProductHost.h"
#include "CandidateChainHost.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateLoopBoundary (Require require)
{
    using namespace live_compare;
    for (const int frames : { 256, 512, 1024 })
        for (const int seconds : { 21, 45 })
            for (int mode = 0; mode < 5; ++mode)
            {
                ProductTestClock wall;
                CandidateChainHost api;
                CandidateHost<Require> host (wall, require, 2, frames);
                for (auto& processor : host.instances)
                {
                    require (LiveTimingFixtureAccess::configureReaperClock (*processor), "loop fixture uses REAPER adapter");
                    processor->getVST3ClientExtensions()->setIHostApplication (&api);
                }
                host.processorTimers = true; host.pair(); auto& post = host.post();
                for (auto& clock : host.clocks) clock.loopLength = seconds * 48000;
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
                for (int n = 0; n < duration; ++n)
                {
                    host.render (1, !ordinary, mode == 1, false);
                    if (ordinary)
                    {
                        bool exact = true;
                        for (int ch = 0; ch < host.channels; ++ch) for (int i = 0; i < frames; ++i)
                            exact = exact && std::abs (host.audio.getSample (ch, i) - candidateInput (
                                host.cursor - frames + i - host.delayA - host.delayB, ch)) < 3.0e-6f;
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
