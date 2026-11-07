#pragma once
#include "CandidateProductHost.h"
#include "CandidateChainHost.h"

namespace hypha::tests
{
template<class Require> void verifyCandidatePostOnlyReturn (Require require)
{
    using namespace live_compare;
    for (const bool postOnly : { false, true })
    {
        ValidationStorageSandbox isolated;
        writeCandidateFixtureIdentity (require, isolated.directory());
        ProductTestClock wall;
        CandidateChainHost api;
        CandidateHost<Require> host (wall, require, 2, 512);
        for (auto& processor : host.instances)
        {
            require (LiveTimingFixtureAccess::configureReaperClock (*processor), "REAPER return fixture clock");
            processor->getVST3ClientExtensions()->setIHostApplication (&api);
        }
        host.processorTimers = true; host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "return fixture starts A/B");
        host.render (300, true, false);
        require (host.replacementBlocks > 0, "return fixture actually replaces input");
        post.finishLiveCompare(); host.render (30, true, false, false);
        host.instances[0].reset(); host.instances[1].reset();
        api.removed = api.sourceRemoved = true; api.postOnly = postOnly;
        api.delay = postOnly ? 0 : 96000;
        if (postOnly) host.delayA = host.delayB = 0;
        const auto render = [&] (int count)
        {
            if (!postOnly) { host.render (count, false, false, false); return; }
            for (int step = 0; step < count; ++step, host.cursor += host.block)
            {
                wall.advanceBlock (host.block, host.sampleRate);
                host.clocks[2].clock = host.cursor; host.clocks[2].delay = 0;
                post.serviceLiveCompare(); LiveTimingFixtureAccess::serviceProcessorTimer (post);
                for (int ch = 0; ch < host.channels; ++ch)
                    for (int i = 0; i < host.block; ++i)
                        host.audio.setSample (ch, i, candidateInput (host.cursor + i, ch));
                host.callback (post);
            }
        };
        api.unavailable = true; render (500);
        require (post.candidateMeasurementIsolated(), "missing chain information keeps capture isolated");
        api.unavailable = false; render (100);
        require (post.candidateMeasurementIsolated(), "current delay and four second settling have not elapsed");
        for (auto& clock : host.clocks) clock.offset -= 7000;
        render (100);
        require (post.candidateMeasurementIsolated(), "seek restarts return progress");
        api.delay += 2048; render (100);
        require (post.candidateMeasurementIsolated(), "delay change restarts return progress");
        host.clocks[2].playing = false; render (5); host.clocks[2].playing = true; render (100);
        require (post.candidateMeasurementIsolated(), "stop restarts return progress");
        host.setSampleRate (44100);
        require (LiveTimingFixtureAccess::configureReaperClock (post), "the re-prepared POST keeps its REAPER fixture clock");
        render (100);
        require (post.candidateMeasurementIsolated(), "rate change restarts return progress");
        render (1000);
        require (!post.candidateMeasurementIsolated() && !post.candidateReturning()
            && !post.outputDecision (output_owner::Activity::recordKeep).refused(),
            "current POST input releases capture exclusion after return");
        render (40); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
        require (post.signalStateLive() == KIRIN_SIGNAL_STATE_ACTIVE, "normal POST measurement resumes");
        for (int n = 0; n < 2; ++n)
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            host.instances[static_cast<std::size_t> (n)] = std::make_unique<KirinHyphaProcessorBase> (KirinHyphaProcessorBase::Role::Pre);
            auto& pre = *host.instances[static_cast<std::size_t> (n)];
            pre.setPlayHead (&host.clocks[static_cast<std::size_t> (n)]);
            prepareFixtureProcessor (pre, host.sampleRate, host.block);
            LiveTimingFixtureAccess::enableFixtureWrites (pre);
            require (LiveTimingFixtureAccess::configureReaperClock (pre), "re-added PRE uses REAPER clock");
            pre.getVST3ClientExtensions()->setIHostApplication (&api);
        }
        api.removed = api.sourceRemoved = api.postOnly = false;
        api.delay = host.delayA + host.delayB;
        host.pair();
        host.requireKeep (post, "re-paired PRE permits real Keep");
        for (int tick = 0; tick < 24 && !post.isRecording(); ++tick)
        { host.render (1, false, false); for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true); }
        require (post.isRecording(), "re-paired Keep reaches Record");
        post.stopPair(); host.render (20, false, false);
        for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true);
        require (post.keepPhase() == KIRIN_KEEP_PHASE_IDLE, "Record stops before another comparison");
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "next comparison starts after re-pair and Record");
        for (auto& processor : host.instances) processor->getVST3ClientExtensions()->setIHostApplication (nullptr);
    }
}
}
