#pragma once
#include "CandidateProductHost.h"
#include "CandidateChainHost.h"

namespace hypha::tests
{
template<class Host, class Require> void requireCandidateRawPost (Host& host, Require require)
{
    for (int ch = 0; ch < host.channels; ++ch)
        for (int i = 0; i < host.block; ++i)
        {
            const auto at = host.cursor - host.block + i - host.delayB;
            require (candidateIdentical (host.audio.getSample (ch, i),
                candidateB (host.history (ch, at), host.history (ch, at - 997))),
                "every POST sample equals the actual B output, independently of the verdict");
        }
}

// The actual product entry must leave no capture exclusion when PRE2 never changed audio.
template<class Require> void verifyCandidateUnusedReturn (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const bool remove : { true, false })
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair(); auto& post = host.post();
        for (auto& clock : host.clocks) clock.available = false;
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "unverified comparison starts through the product entry");
        if (remove) host.instances[1].reset(); // no PRE2 service or callback since START
        else { host.render (100, false, false); post.finishLiveCompare(); }
        host.render (10, false, false);
        require (!post.candidateMeasurementIsolated() && !post.candidateReturning(),
                 "a withdrawn unused request releases measurement without ever receiving a clock");
        require (!post.outputDecision (output_owner::Activity::recordKeep).refused(),
                 "unused END removes the comparison exclusion even without a clock");
        for (auto& clock : host.clocks) clock.available = true;
        host.render (40, false, false);
        for (auto& processor : host.instances) if (processor)
        { awaitAnalysisInput (*processor); awaitAnalysisExchange (*processor, true); }
        host.requireKeep (post, "unused END releases the real Keep admission");
        for (int tick = 0; tick < 24 && !post.isRecording(); ++tick)
        {
            host.render (1, false, false);
            for (auto& processor : host.instances) if (processor) awaitAnalysisExchange (*processor, true);
        }
        require (post.isRecording(), "the next ordinary Keep reaches Record before fixture teardown");
        post.stopPair(); host.render (20, false, false);
        for (auto& processor : host.instances) if (processor) awaitAnalysisExchange (*processor, true);
        require (post.keepPhase() == KIRIN_KEEP_PHASE_IDLE, "the fixture completes Stop before another capture transaction");
    }
    // A partial first fade is already changed output, even with no fully replaced receipt.
    CandidateHost<Require> host (wall, require, 1, 64);
    host.pair(); auto& post = host.post();
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
             "first-fade comparison starts");
    bool faded = false;
    for (int i = 0; i < 300 && !faded; ++i)
    {
        host.render (1, true, false, false);
        for (int frame = 0; frame < host.block; ++frame)
            faded = faded || !candidateIdentical (host.history (0, host.cursor - host.block + frame),
                candidateA (host.cursor - host.block + frame - host.delayA, 0));
    }
    require (faded && host.replacementBlocks == 0, "the first partial fade changed audio before any exact replacement block");
    for (auto& clock : host.clocks) clock.available = false;
    post.finishLiveCompare(); host.render (20, true, false, false);
    require (post.candidateMeasurementIsolated() && !post.keepPair(),
             "even a partial fade needs return proof while clocks are absent");
    for (auto& clock : host.clocks) clock.available = true;
    host.render (3400, false, false);
    require (!post.candidateMeasurementIsolated(), "fresh clocks automatically drain a partial first fade");
}

template<class Require> void verifyCandidateRateReturn (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const int mode : { 0, 1, 2 })
    {
        const bool loop = mode != 0;
        CandidateHost<Require> host (wall, require, 2, 512, loop);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "sample-rate return starts through the product entry");
        host.render (180, true, false);
        require (host.replacementBlocks > 100, "rate change follows actual replacement");
        if (mode == 2)
        {
            post.finishLiveCompare(); host.render (10, true, false, false); host.render (360, false, false);
            const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
            require (post.candidateMeasurementIsolated() && state.quietFrames.load() > 176400,
                     "pending return has more old-rate frames than the new-rate threshold");
        }
        if (loop) for (auto& clock : host.clocks) clock.loopLength = 22050;
        host.setSampleRate (44100);
        host.render (1, false, false);
        require (post.candidateMeasurementIsolated(), "old-rate settling cannot release a new format before a fresh callback proof");
        host.render (100, false, false);
        require (post.candidateMeasurementIsolated(), "new-rate return still excludes the downstream settling history");
        host.render (800, false, false);
        require (!post.candidateMeasurementIsolated() && !post.candidateReturning(),
                 "current-rate source markers release isolation while playback and short loops continue");
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "the new sample rate admits the next explicit comparison");
        const auto before = host.replacementBlocks;
        // Loop reacquisition can leave B's echo carrying earlier real input. Compare its
        // complete actual history, rather than pretending an arbitrary tail is zero-state B(x).
        for (int i = 0; i < 180; ++i)
        { host.render (1, true, false, false); requireCandidateRawPost (host, require); }
        require (host.replacementBlocks > before + 100 && post.liveCompareStatus().verdict == Verdict::accepted,
                 "the new sample rate renders verified audio after return");
    }
}

// Keep the generic loop running throughout withdrawal and real capture/comparison admission.
// PRE1 keeps publishing fresh clocks, but neither reader may infer an ambiguous loop lap.
template<class Require> void verifyCandidateGenericLoopReturn (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const bool remove : { false, true })
    {
        CandidateHost<Require> host (wall, require, remove ? 2 : 1, 512);
        host.pair(); auto& post = host.post();
        const auto selected = host.pre2().instanceId();
        require (post.startCandidateCompare (selected, true) == StartResult::started,
                 "generic-loop return starts through the real candidate API");
        post.selectLiveComparePre (true); host.render (180, true, true);
        require (host.replacementBlocks > 100 && post.liveCompareStatus().preAudible,
                 "linear playback actually substitutes x and plays A before entering the generic loop");
        for (auto& clock : host.clocks) clock.loopLength = 24000;
        host.render (100, true, true, false);
        require (post.liveCompareStatus().verdict != Verdict::accepted,
                 "an updating generic loop never grants ambiguous PCM or MATCH");
        const auto replaced = host.replacementBlocks;
        if (remove) host.instances[1].reset();
        else post.finishLiveCompare();
        host.render (12, true, false, false);
        host.render (200, false, false);
        require (post.candidateMeasurementIsolated() && !post.keepPair(),
                 "withdrawal still drains the validated bound before ordinary measurement or Keep");
        host.render ((static_cast<int> (ringCapacityFrames) + 192000) / host.block + 40, false, false);
        require (post.candidateMeasurementIsolated() && post.candidateReturning() && !post.keepPair(),
                 "an ambiguous loop cannot release isolation merely because the ring capacity elapsed");
        TimingSnapshot fresh;
        const auto* source = LiveTimingFixtureAccess::publishedRing (host.pre1());
        require (source && readTiming (source->ring()->header.timing, fresh) && fresh.active
            && fresh.block.loop.active && wall.nanos() - fresh.block.wallNanos < CandidateTransfer::leaseNanos,
                 "PRE1 remains fresh during ambiguous loop return");
        for (auto& clock : host.clocks) clock.loopLength = 0;
        host.render (1200, false, false);
        require (!post.candidateMeasurementIsolated() && !post.candidateReturning(),
                 "fresh linear correspondence automatically completes the return");
        for (auto& processor : host.instances) if (processor)
        { awaitAnalysisInput (*processor); awaitAnalysisExchange (*processor, true); }
        require (post.signalStateLive() == KIRIN_SIGNAL_STATE_ACTIVE,
                 "canonical POST measurement resumes after current linear proof");
        host.requireKeep (post, "real Keep admission resumes after current linear proof");
        for (int tick = 0; tick < 24 && !post.isRecording(); ++tick)
        {
            host.render (1, false, false);
            for (auto& processor : host.instances) if (processor) awaitAnalysisExchange (*processor, true);
        }
        require (post.isRecording(), "ordinary Keep reaches actual Record after current linear proof");
        post.stopPair(); host.render (20, false, false);
        for (auto& processor : host.instances) if (processor) awaitAnalysisExchange (*processor, true);
        require (post.keepPhase() == KIRIN_KEEP_PHASE_IDLE, "loop Keep completes Stop before the next comparison");
        const auto next = remove ? post.startLiveCompare() : post.startCandidateCompare (selected, true);
        require (next == StartResult::started, "the next explicit comparison is admitted after return");
        for (auto& clock : host.clocks) clock.loopLength = 24000;
        host.render (100, true, false, false);
        require (post.liveCompareStatus().verdict != Verdict::accepted && host.replacementBlocks == replaced,
                 "return-only drain never grants comparison PCM permission to the uncertified loop");
        requireCandidateRawPost (host, require);
    }
}

template<class Require> void verifyCandidateRecoveryAudit (Require require)
{
    std::cout << "Candidate recovery: generic loop\n" << std::flush;
    verifyCandidateGenericLoopReturn (require);
    std::cout << "Candidate recovery: unused\n" << std::flush;
    verifyCandidateUnusedReturn (require);
    std::cout << "Candidate recovery: rate\n" << std::flush;
    verifyCandidateRateReturn (require);
    using namespace live_compare;
    ProductTestClock wall;
    for (const int channels : { 1, 2 })
    {
        CandidateHost<Require> host (wall, require, channels, 512);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "clock-hole candidate starts");
        host.render (180, true, false);
        const auto replaced = host.replacementBlocks;
        host.clocks[2].available = false; host.render (1, true, false, false);
        require (host.replacementBlocks == replaced + 1, "only POST's clock is missing; PRE2 still supplies x");
        requireCandidateRawPost (host, require);
        host.clocks[2].available = true; host.render (100, true, false);
        post.selectLiveComparePre (true); host.render (10, true, true, false);
        post.getBypassParameter()->setValue (1.0f); host.render (4, true, false, false);
        host.clocks[2].available = false;
        post.getBypassParameter()->setValue (0.0f); host.render (1, true, true, false);
        requireCandidateRawPost (host, require);
        host.clocks[2].available = true; host.render (100, true, true);
        host.pre2().getBypassParameter()->setValue (1.0f); host.render (1, true, true, false); host.render (3, false, true, false);
        host.clocks[0].available = false;
        host.pre2().getBypassParameter()->setValue (0.0f); host.render (1, false, true, false);
        // render(..., false, ...) asserts PRE2 bit identity from the first resumed sample.
        host.clocks[0].available = true; host.render (100, true, true);
        host.pre2().getBypassParameter()->setValue (1.0f); host.render (1, true, true, false); host.render (3, false, true, false);
        post.finishLiveCompare(); host.render (650, false, false);
        require (!post.candidateMeasurementIsolated() && !post.candidateReturning(), "END completes while PRE2 remains bypassed with callbacks");
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "bypassed END releases the real capture exclusion for the next explicit comparison");
    }
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.delayB = 0; host.pair(); auto& post = host.post();
        post.setLiveCompareAutoAlign (true);
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "aligned input-proof candidate starts");
        host.render (950, true, false);
        auto measured = post.measureLiveCompareOffset(); measured.clockK = 0; measured.lagFrames = -1024;
        LiveTimingFixtureAccess::publishMeasuredAlign (post, measured); host.render (2, true, false, false);
        require (post.liveCompareAlignFrames() == 1024 && LiveTimingFixtureAccess::candidateHistoryValid (post),
                 "candidate has a multi-block A correction and a settled MATCH history");
        host.clocks[0].available = false; host.render (1, true, false, false);
        require (post.liveCompareStatus().verdict != Verdict::accepted && !LiveTimingFixtureAccess::candidateHistoryValid (post),
                 "an actual B-input hole cannot borrow the older aligned A receipt for hearing or MATCH");
        requireCandidateRawPost (host, require);
        host.clocks[0].available = true; host.render (12, true, false, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted && !LiveTimingFixtureAccess::candidateHistoryValid (post),
                 "the chosen B resumes automatically but its new settling interval is excluded from MATCH");
    }
    // Generic linear seek and certified engine loops also recover without PRE2's retired clock.
    for (const bool loop : { false, true })
    {
        CandidateHost<Require> host (wall, require, loop ? 2 : 1, 512, loop);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "removed-source candidate starts");
        if (loop) for (auto& clock : host.clocks) clock.loopLength = 48000;
        for (int i = 0; i < 220; ++i)
        { host.render (1, true, false, false); requireCandidateRawPost (host, require); }
        host.render (40, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "candidate is audible before source removal");
        host.instances[1].reset(); host.render (2, false, false);
        if (!loop)
        {
            for (auto& clock : host.clocks) clock.offset -= 60000;
            host.delayB = 240000;
            host.render (410, false, false);
            require (post.candidateMeasurementIsolated(), "removal plus seek cannot drain a newly increased five-second B delay early");
            post.kirinHostDelayCompensationStateChanged (false); host.render (500, false, false);
            require (post.candidateMeasurementIsolated(), "uncompensated clocks never release the return quarantine");
            post.kirinHostDelayCompensationStateChanged (true);
        }
        host.render (1000, false, false);
        require (!post.candidateMeasurementIsolated(), "PRE2 removal releases isolation through looping or a later seek");
    }
    std::cout << "Candidate recovery: chain\n" << std::flush;
    // Real adapter plus real processor timer: END does not switch the still-observed chain
    // back to the one-PRE topology. Removal changes the host report, not an injected proof.
    enum class ReturnAction { end, remove, bypass, moveHardware, removeBoth };
    for (const auto action : { ReturnAction::end, ReturnAction::remove, ReturnAction::bypass, ReturnAction::moveHardware, ReturnAction::removeBoth })
    {
        const bool remove = action == ReturnAction::remove || action == ReturnAction::removeBoth;
        const bool bypass = action == ReturnAction::bypass;
        CandidateChainHost api;
        CandidateHost<Require> host (wall, require, 2, 512);
        for (auto& processor : host.instances)
        {
            require (LiveTimingFixtureAccess::configureReaperClock (*processor), "certified REAPER clock fixture");
            processor->getVST3ClientExtensions()->setIHostApplication (&api);
        }
        host.processorTimers = true; host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "REAPER candidate starts");
        for (auto& clock : host.clocks) clock.loopLength = 48000;
        for (int i = 0; i < 220; ++i)
        { host.render (1, true, false, false); requireCandidateRawPost (host, require); }
        host.render (40, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "real REAPER adapters prove the looping candidate");
        if (remove)
        {
            host.instances[1].reset(); api.removed = true;
            if (action == ReturnAction::removeBoth) { host.instances[0].reset(); api.sourceRemoved = true; }
            host.delayB = 16384; api.delay = host.delayA + host.delayB;
            for (auto& clock : host.clocks) clock.offset -= 7000;
        }
        else
        {
            if (bypass)
            { api.bypassed = true; host.pre2().getBypassParameter()->setValue (1.0f); }
            if (action == ReturnAction::moveHardware)
            {
                const auto before = host.replacementBlocks;
                api.hardwareA = true; host.delayA += 2048; api.delay = host.delayA + host.delayB;
                host.render (180, true, false);
                require (host.replacementBlocks > before + 100
                    && post.liveCompareStatus().reaperRestriction == ReaperChainRestriction::none,
                    "moving hardware to A re-proves its delay and resumes replacement automatically");
            }
            post.finishLiveCompare();
        }
        if (action == ReturnAction::removeBoth)
        {
            api.unavailable = true; host.render (500, false, false);
            require (post.candidateMeasurementIsolated(), "withdrawal without current chain data cannot release isolation");
            api.unavailable = false; host.render (100, false, false);
            require (post.candidateMeasurementIsolated(), "both removed PREs still require downstream drain and settling");
        }
        host.render (8, !remove, false, false); host.render (750, false, false);
        if (post.candidateMeasurementIsolated())
        {
            std::cerr << "REAPER return remains pending; removed=" << remove << " bypassed=" << bypass << '\n';
            const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
            const auto* observer = state.returnObserver.control();
            if (observer) std::cerr << "quiet=" << state.quietFrames.load() << " seen=" << observer->seen
                << " marked=" << observer->marked << " marker=" << observer->marker << '\n';
            for (auto& processor : host.instances) if (processor)
            {
                const auto* mapping = LiveTimingFixtureAccess::publishedRing (*processor);
                TimingSnapshot clock; readTiming (mapping->ring()->header.timing, clock);
                const auto& transfer = mapping->ring()->header.candidate;
                std::cerr << "role=" << (processor.get() == &post) << " restriction=" << LiveTimingFixtureAccess::reaperRestrictionForTest (*processor)
                    << " demand=" << mapping->ring()->header.demand.load() << " readers=" << transfer.readers.load()
                    << " owner=" << transfer.owner.load() << " withdrawn=" << transfer.withdrawn (transfer.owner.load())
                    << " active=" << clock.active << " generation=" << clock.generation
                    << " runtime=" << clock.block.runtimeRevision << " chain=" << clock.block.runtimeChainA << '\n';
            }
        }
        require (!post.candidateMeasurementIsolated(), "REAPER loop continues through END, bypass or removal and completes return");
        require (!post.outputDecision (output_owner::Activity::recordKeep).refused(), "return releases capture exclusion");
        if (!api.sourceRemoved)
        {
            require (post.startLiveCompare() == StartResult::started, "REAPER return releases comparison admission");
            post.selectLiveComparePre (true);
            host.render (220, false, true, false);
            require (post.liveCompareStatus().preAudible, "ordinary LISTEN actually selects PRE1 while two PREs remain in a loop");
            for (int ch = 0; ch < host.channels; ++ch) for (int i = 0; i < host.block; ++i)
                require (std::abs (host.audio.getSample (ch, i) - candidateInput (
                    host.cursor - host.block + i - host.delayA - host.delayB, ch)) < 1.0e-7f,
                    "ordinary LISTEN output equals independently indexed PRE1 after candidate END");
        }
        else
        {
            host.render (40, false, false); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
            require (post.signalStateLive() == KIRIN_SIGNAL_STATE_ACTIVE, "POST measurement resumes without either old PRE");
        }
        for (auto& processor : host.instances) if (processor) processor->getVST3ClientExtensions()->setIHostApplication (nullptr);
    }
}
}
