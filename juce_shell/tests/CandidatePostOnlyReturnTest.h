#pragma once
#include "CandidateProductHost.h"
#include "CandidateChainHost.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateTailWithdrawalReceipt (Require require, bool aax)
{
    using namespace live_compare;
    SharedRingMapping original, receiver;
    const auto originalKey = pairKeyForPreInstance (juce::Uuid().toString().toStdString());
    const auto receiverKey = pairKeyForPreInstance (juce::Uuid().toString().toStdString());
    require (original.create (originalKey, 48000) && receiver.create (receiverKey, 48000), "tail receipt fixture rings");
    auto observer = std::make_unique<CandidateReturnObserver>();
    require (observer->prepare (originalKey, 48000), "tail receipt source observer");
    observer->withdrawal = std::make_shared<SharedRingMapping>();
    require (observer->withdrawal->open (receiverKey, 48000, false), "tail receipt receiver observer");
    CandidateProcessorState state;
    require (state.returnObserver.publish (std::move (observer)), "tail receipt observer publication");
    constexpr std::uint64_t request = 7;
    state.tail.store (request);
    BlockClock block;
    block.frames = 512; block.playing = block.clockValid = block.projectValid = true;
    block.clockBasis = static_cast<std::uint8_t> (aax ? ClockBasis::aaxEngine : ClockBasis::reaperProcessing);
    block.clockAuthority = static_cast<std::uint8_t> (aax ? ClockAuthority::boundedAaxEngine : ClockAuthority::certifiedProject);
    block.maximumDelaySamples = aax ? 48000 : 0;
    block.runtimeChainA = 1; block.runtimeChainB = 2; block.runtimeRevision = 1;
    const auto advance = [&] (int count)
    {
        for (int i = 0; i < count; ++i)
        {
            block.wallNanos += 10'666'667; state.observeTail (block, 48000, true);
            block.clock += block.frames; block.project += block.frames;
        }
    };
    receiver.ring()->header.candidate.acknowledgeReturn (request, block);
    advance (600);
    require (state.quietFrames.load() == 0, "a present original source without a marker cannot be replaced by time alone");
    receiver.ring()->header.candidate.revokeReturn (request);
    original.close();
    advance (600);
    require (state.quietFrames.load() == 0, "an unavailable original source cannot replace a withdrawal receipt");
    receiver.ring()->header.candidate.acknowledgeReturn (request, block);
    advance (500);
    require (state.quietFrames.load() >= 4 * 48000, "the matching withdrawal allows bounded drainage and settling");
}

template<class Require> void verifyCandidateExpiredSourceTail (Require require, bool removeAfterDeadline)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    ProductTestClock wall;
    CandidateChainHost api;
    CandidateHost<Require> host (wall, require, 2, 512);
    for (auto& processor : host.instances)
    {
        require (LiveTimingFixtureAccess::configureReaperClock (*processor), "expired-source fixture clock");
        processor->getVST3ClientExtensions()->setIHostApplication (&api);
    }
    host.processorTimers = true; host.pair(); auto& post = host.post();
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
             "expired-source A/B starts");
    host.render (300, true, false, false);
    require (host.replacementBlocks > 0, "expired-source fixture replaces input");
    post.finishLiveCompare(); host.render (30, true, false, false);
    host.pre1Enabled = false; api.unavailable = true;
    for (int n = 0; n < 1100 && post.candidateReturning(); ++n) host.render (1, false, false, false);
    require (!post.candidateReturning() && post.candidateTailPending() && !post.keepPair(),
             "expired original source and missing host bound leave the deadline tail protected");
    if (removeAfterDeadline)
    {
        host.setSampleRate (44100);
        for (auto& processor : host.instances)
            require (LiveTimingFixtureAccess::configureReaperClock (*processor), "expired-source reprepare clock");
        host.render (1, false, false, false);
        host.instances[0].reset(); api.sourceRemoved = true;
        host.render (1, false, false, false);
        require (post.candidateTailPending() && !post.keepPair(),
                 "removing the original PRE after the deadline keeps the tail protected");
    }
    api.unavailable = false;
    host.render (1200, false, false, false);
    require (!post.candidateTailPending(), "a current bound drains the expired-source tail and settles");
    if (!removeAfterDeadline)
    {
        host.pre1Enabled = true; host.pair();
        host.requireKeep (post, "Keep resumes after the expired-source tail clears");
    }
    for (auto& processor : host.instances)
        if (processor) processor->getVST3ClientExtensions()->setIHostApplication (nullptr);
}

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
        // Every restart below fits inside the return's overall ten seconds after END.
        api.unavailable = true; render (380);
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
        for (int n = 0; n < 1000 && post.candidateReturning(); ++n) render (1);
        require (!post.candidateMeasurementIsolated() && !post.candidateReturning()
            && !post.outputDecision (output_owner::Activity::recordKeep).refused(),
            "current POST input releases capture exclusion after return");
        require (post.candidateTailPending() && !post.keepPair() && !post.keepAll(),
                 "the deadline and removal of the original PRE leave Keep protected");
        host.clocks[2].playing = false; render (500); host.clocks[2].playing = true;
        require (post.candidateTailPending() && !post.keepPair(), "stopped callbacks cannot drain the tail");
        for (int n = 0; n < 8; ++n)
        {
            for (auto& clock : host.clocks) clock.offset -= 7000;
            render (100);
            require (post.candidateTailPending() && !post.keepPair(), "repeated seeks restart the tail proof");
        }
        api.unavailable = true; render (600);
        require (post.candidateTailPending() && !post.keepPair(),
                 "elapsed playback without a current host bound cannot clear the tail");
        api.unavailable = false;
        render (200); api.delay += 2048; render (200);
        require (post.candidateTailPending() && !post.keepPair(), "delay change restarts the tail proof");
        host.setSampleRate (48000);
        require (LiveTimingFixtureAccess::configureReaperClock (post), "the tail retains its fixture clock after reprepare");
        render (200);
        require (post.candidateTailPending() && !post.keepPair(), "rate change restarts the tail proof");
        for (auto& clock : host.clocks) clock.offset -= 7000;
        api.unavailable = true; render (1); api.unavailable = false;
        render (40); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
        require (measurementActiveAfterCallback (post, [&] { render (1); }), "normal POST measurement resumes");
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
        require (post.candidateTailPending() && !post.keepPair(),
                 "a different paired PRE does not itself clear the old tail");
        host.render (100, false, false, false);
        require (post.candidateTailPending() && !post.keepPair(),
                 "bounded processing alone cannot skip the four-second settling period");
        // The current host's delay bound plus scheduling allowance must pass before the
        // four-second settling interval; requireKeep alone does not advance enough callbacks.
        host.render (1600, false, false, false);
        require (!post.candidateTailPending(), "continuous bounded processing drains the old tail");
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
