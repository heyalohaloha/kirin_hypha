#pragma once
#include "CandidateProductHost.h"
#include "CandidateChainHost.h"
#include <functional>
#include <future>
#include <thread>
#include <utility>

namespace hypha::tests
{
inline std::function<void()> candidateServiceOverlap;

// Exercise the real timer's final continuation check, including an ordinary PRE with no A/B.
// The hook runs after reading the mapping and before dereferencing it, exactly at the old race.
template<class Require> void verifyCandidateTimerLifetime (Require require)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    ProductTestClock wall;
    for (const bool comparing : { false, true })
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair(); auto& pre = comparing ? host.pre2() : host.pre1();
        if (comparing)
            require (host.post().startCandidateCompare (pre.instanceId(), true) == StartResult::started,
                     "timer lifetime A/B starts through the real entry");
        host.render (20, comparing, false, false);
        std::promise<void> entered, prepared;
        auto entering = entered.get_future(); auto done = prepared.get_future();
        std::thread other;
        bool overlapped = false, checked = false;
        candidateServiceOverlap = [&]
        {
            checked = true;
            other = std::thread ([&]
            {
                entered.set_value();
                prepareFixtureProcessor (pre, 44100, host.block);
                prepared.set_value();
            });
            entering.wait();
            overlapped = done.wait_for (std::chrono::milliseconds (300)) == std::future_status::ready;
        };
        afterLiveCompareServiceReadForTest = [] { if (auto run = std::exchange (candidateServiceOverlap, nullptr)) run(); };
        LiveTimingFixtureAccess::serviceProcessorTimer (pre);
        afterLiveCompareServiceReadForTest = nullptr;
        require (checked, "the actual timer reaches its mapping lifetime check");
        other.join();
        require (!overlapped, "re-prepare cannot free the mapping under the timer continuation check");
        prepareFixtureProcessor (pre, 48000, host.block);
        LiveTimingFixtureAccess::enableFixtureWrites (pre);
        std::promise<void> locked, release;
        auto acquired = locked.get_future(); auto unlock = release.get_future();
        std::thread owner ([&]
        {
            const juce::ScopedLock hold (LiveTimingFixtureAccess::handleOwnership (pre));
            locked.set_value(); unlock.wait();
        });
        acquired.wait();
        const auto continueService = LiveTimingFixtureAccess::liveCompareNeedsService (pre);
        release.set_value(); owner.join();
        require (continueService, "a busy non-RT owner keeps the service timer alive without waiting");
        host.render (520, false, false);
        require (!host.post().candidateMeasurementIsolated(), "timer re-prepare still completes any pending return");
    }
}

// Bypassing need not destroy a PRE. Its stale mapping cannot keep normal POST measurement,
// Keep and the next explicit comparison waiting forever after a seek and END.
template<class Require> void verifyCandidateStoppedSourceReturn (Require require)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512, true);
    host.pair(); auto& post = host.post();
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "bypass return starts");
    host.render (180, true, false);
    require (host.replacementBlocks > 100, "both PREs stop only after actual source replacement");
    host.pre1().getBypassParameter()->setValue (1.0f); host.pre2().getBypassParameter()->setValue (1.0f);
    host.pre1Enabled = host.pre2Enabled = false;
    for (auto& clock : host.clocks) clock.offset -= 96000;
    post.finishLiveCompare();
    host.render (300, false, false);
    const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
    const auto* source = LiveTimingFixtureAccess::publishedRing (host.pre1());
    require (source && source->ring()->matches (source->key(), 48000), "bypassed PRE1 keeps its original shared mapping");
    require (post.candidateMeasurementIsolated() && !post.keepPair(), "END still excludes capture before the drain bound");
    require (state.returnSettling.withdrawnFrames > 0, "a stale PRE1 mapping does not prevent return-only drainage");
    // Changing the timeline partway through drainage must start the bound again.
    for (auto& clock : host.clocks) clock.offset -= 96000;
    host.render (1, false, false);
    require (state.quietFrames.load() == 0 && state.returnSettling.withdrawnFrames <= static_cast<std::uint64_t> (host.block)
        && !state.returnContinuity.known, "seek resets drainage and a stopped PRE cannot supply a new projection proof");
    // The host's delay bound and the four-second settling both count again from this seek.
    const auto bound = candidateReturnDrainFrames (state.returnSettling.previous);
    require (bound.has_value(), "the qualified host supplies the drain bound");
    host.render (static_cast<int> ((*bound + 192000) / static_cast<std::uint64_t> (host.block)) - 8, false, false);
    require (post.candidateMeasurementIsolated(), "a second seek cannot reuse already counted return frames");
    host.render (900, false, false);
    require (!post.candidateMeasurementIsolated() && !post.candidateReturning(), "END completes with both stopped PREs still installed");
    host.render (40, false, false); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
    require (measurementActiveAfterCallback (post, [&] { host.render (1, false, false); }),
             "ordinary POST measurement resumes on its actual input");
    host.pre1().getBypassParameter()->setValue (0.0f); host.pre2().getBypassParameter()->setValue (0.0f);
    host.pre1Enabled = host.pre2Enabled = true;
    host.render (100, false, false);
    for (auto& processor : host.instances) { awaitAnalysisInput (*processor); awaitAnalysisExchange (*processor, true); }
    host.requireKeep (post, "Keep starts again through its real admission");
    for (int i = 0; i < 24 && !post.isRecording(); ++i)
    { host.render (1, false, false); for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true); }
    require (post.isRecording(), "the recovered Keep reaches real Record");
    post.stopPair(); host.render (20, false, false);
    for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "another explicit A/B starts without re-pairing");
    host.render (100, true, false);
    require (post.liveCompareStatus().verdict == Verdict::accepted, "fresh current positions resume correct comparison audio");
}

// A host may re-prepare PRE2 on its control thread while the message-thread timer holds the
// receiver. Both overlap at the exact read: the prepare must wait for that service to finish.
template<class Require> void verifyCandidateReceiverLifetime (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post(); auto& pre2 = host.pre2();
    require (post.startCandidateCompare (pre2.instanceId(), true) == StartResult::started, "lifetime fixture starts");
    host.render (20, true, false, false);
    require (LiveTimingFixtureAccess::candidateStateForTest (pre2).receiver.control() != nullptr, "PRE2 owns a receiver");
    std::promise<void> prepared;
    auto done = prepared.get_future();
    std::thread other;
    bool overlapped = false;
    candidateServiceOverlap = [&]
    {
        other = std::thread ([&] { prepareFixtureProcessor (pre2, 44100, host.block); prepared.set_value(); });
        overlapped = done.wait_for (std::chrono::milliseconds (300)) == std::future_status::ready;
    };
    afterCandidateReceiverReadForTest = [] { if (auto run = std::exchange (candidateServiceOverlap, nullptr)) run(); };
    pre2.serviceLiveCompare();
    afterCandidateReceiverReadForTest = nullptr;
    other.join();
    require (!overlapped, "a concurrent re-prepare cannot free the receiver under the service timer");
    prepareFixtureProcessor (pre2, 48000, host.block);
    LiveTimingFixtureAccess::enableFixtureWrites (pre2);
    host.render (520, false, false);
    require (!post.candidateMeasurementIsolated(), "the serialized re-prepare still completes the return");
}

// Another pair of the same project: a real second POST with its own PRE. Its POST runs after the
// first POST; its PRE does too, unless it reads the same input as PRE1 to use the same PRE2.
struct CandidateOtherPair
{
    using P = KirinHyphaProcessorBase;
    std::unique_ptr<P> pre = std::make_unique<P> (P::Role::Pre), post = std::make_unique<P> (P::Role::Post);
    template<class Host, class Require> CandidateOtherPair (Host& host, Require require, bool beforePre2 = false)
    {
        for (auto* processor : { pre.get(), post.get() })
        {
            auto layout = processor->getBusesLayout();
            const auto set = host.channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
            require (processor->setBusesLayout (layout), "the other pair negotiates the host layout");
            const bool beforeChain = beforePre2 && processor == pre.get();
            processor->setPlayHead (&host.clocks[beforeChain ? 0 : 2]);
            prepareFixtureProcessor (*processor, host.sampleRate, host.block);
            LiveTimingFixtureAccess::enableFixtureWrites (*processor);
            (beforeChain ? host.sourcePassengers : host.passengers).push_back (processor);
        }
        host.render (40, false, false);
        for (auto* processor : { pre.get(), post.get() })
        { awaitAnalysisInput (*processor); awaitAnalysisExchange (*processor, true); }
        // PRE announces itself from its IO worker, on that thread's own schedule.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (30);
        bool paired = false;
        while (!paired && std::chrono::steady_clock::now() < deadline)
        {
            paired = post->setPairCandidate (pre->instanceId(), "Other");
            if (!paired)
            {
                host.render (1, false, false);
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
            }
        }
        require (paired, "the other POST pairs");
    }
    ~CandidateOtherPair() { for (auto* processor : { pre.get(), post.get() }) processor->releaseResources(); }
};

// A format change inside the return keeps the project's capture exclusion on the new engine.
template<class Require> void verifyCandidateHeldExclusion (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    CandidateOtherPair other (host, require);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "A/B starts before the rate change");
    host.render (180, true, false);
    require (host.replacementBlocks > 100, "the rate change follows actual replacement");
    host.setSampleRate (44100);
    host.render (2, false, false);
    require (post.candidateMeasurementIsolated(), "the return is still pending at the new rate");
    require (post.outputDecision (output_owner::Activity::recordKeep).refused(), "this POST keeps refusing Keep");
    require (!other.post->keepAll() && !other.post->keepPair() && !post.isRecording() && !other.post->isRecording(),
             "All Keep from another POST cannot reach the isolated POST across the engine rebuild");
    host.render (520, false, false);
    require (!post.candidateMeasurementIsolated(), "the return completes at the new rate");
    for (auto& processor : host.instances) { awaitAnalysisInput (*processor); awaitAnalysisExchange (*processor, true); }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (10);
    bool started = other.post->keepAll();
    while (!started && std::chrono::steady_clock::now() < deadline)
    {
        host.render (1, false, false);
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
        started = other.post->keepAll();
    }
    require (started, "the same exclusion is released with the return");
    for (int tick = 0; tick < 48 && !(post.isRecording() && other.post->isRecording()); ++tick)
    {
        host.render (1, false, false);
        for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true);
        for (auto* processor : { other.pre.get(), other.post.get() }) awaitAnalysisExchange (*processor, true);
    }
    require (post.isRecording() && other.post->isRecording(), "All Keep reaches actual Record on both POSTs after the return");
    other.post->stopPair(); post.stopPair(); host.render (20, false, false);
    for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true);
    for (auto* processor : { other.pre.get(), other.post.get() }) awaitAnalysisExchange (*processor, true);
    host.passengers.clear();
}

// One PRE2 serves one request at a time, but each POST owns its own return. The project exclusion
// refuses a second POST during the return; a later request in the same ring still erases nothing.
template<class Require> void verifyCandidateSharedPre2 (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    CandidateOtherPair other (host, require, true); // a PRE belongs to one POST, so this POST has its own
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "first POST starts");
    host.render (180, true, false);
    post.finishLiveCompare();
    host.render (12, true, false, false);
    require (post.candidateReturning(), "the first POST is still draining");
    require (other.post->startCandidateCompare (host.pre2().instanceId(), true) == StartResult::comparisonBusy,
             "the project exclusion holds a second POST until the return completes");
    auto* mapping = const_cast<SharedRingMapping*> (LiveTimingFixtureAccess::publishedRing (host.pre2()));
    auto& transfer = mapping->ring()->header.candidate;
    require (transfer.begin (0x1235, transfer.source.load(), transfer.sourceOwnerA.load(), transfer.sourceOwnerB.load(), wall.nanos()),
             "the PRE2 slot itself is free once the first request has withdrawn");
    host.render (520, true, false, false);
    require (!post.candidateMeasurementIsolated() && !post.candidateReturning(),
             "a later request on the same PRE2 leaves the first POST's return intact");
    host.render (120, false, false);
    require (other.post->startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
             "the next POST starts once the return has released the project");
    host.render (120, true, false, false);
    require (other.post->candidateCompareActive(), "the second comparison holds its own request");
    host.passengers.clear(); host.sourcePassengers.clear();
}

// Losing both PREs is never a reason to keep POST out of normal measurement.
template<class Require> void verifyCandidateBothPresRemoved (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    {
        CandidateHost<Require> host (wall, require, 2, 512, true);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "Pro Tools loop starts");
        for (auto& clock : host.clocks) clock.loopLength = 48000;
        for (int i = 0; i < 220; ++i) { host.render (1, true, false, false); requireCandidateRawPost (host, require); }
        host.render (40, true, false);
        require (host.replacementBlocks > 100, "the comparison actually replaced B's input");
        host.instances[1].reset(); host.instances[0].reset();
        host.render (480, false, false);
        require (!post.candidateMeasurementIsolated() && !post.candidateReturning(),
                 "the loop continues and POST returns to normal measurement without either PRE");
        require (!post.outputDecision (output_owner::Activity::recordKeep).refused(), "the capture exclusion is gone");
        host.render (40, false, false); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
        require (measurementActiveAfterCallback (post, [&] { host.render (1, false, false); }), "POST measures its own input again");
    }
    // A passed upstream marker remains sufficient when that PRE is later removed.
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "generic host starts");
    host.render (180, true, false);
    require (host.replacementBlocks > 100, "generic comparison replaced B's input");
    host.instances[1].reset(); host.render (40, false, false);
    const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
    const auto& settling = state.returnSettling;
    const auto* observer = state.returnObserver.control();
    require (observer && observer->proofAvailable && observer->marked && settling.drained
        && state.quietFrames.load() > 0
        && post.candidateMeasurementIsolated(), "fresh PRE1 correspondence confirms return");
    host.instances[0].reset(); host.render (40, false, false);
    require (settling.drained && state.quietFrames.load() > 0, "a passed marker keeps its settling progress after PRE removal");
}

// x -> a across a host bypass follows the same ramp as every other return, and only once.
template<class Require> void verifyCandidateBypassFade (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const int frames : { 128, 512 })
    {
        CandidateHost<Require> host (wall, require, 2, frames);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "bypass fixture starts");
        host.render (72000 / frames, true, false, false);
        require (host.replacementBlocks > 0, "x reaches B before the bypass");
        const auto start = host.cursor;
        host.pre2().getBypassParameter()->setValue (1.0f);
        host.render ((960 + frames - 1) / frames, true, false, false);
        for (int ch = 0; ch < host.channels; ++ch)
            for (int i = 0; i < 960; ++i)
            {
                const auto input = candidateA (start + i - host.delayA, ch);
                const auto value = host.history (ch, start + i);
                if (i >= 240) require (candidateIdentical (value, input), "after the fade PRE2 is exact pass-through");
                else
                {
                    const auto weight = static_cast<float> (i) / 240.0f;
                    require (std::abs (value - (host.history (ch, start - 1) * (1.0f - weight) + input * weight)) < 1.0e-6f,
                             "a bypass while x plays fades from the last output into the actual input");
                }
            }
        if (frames != 512) continue;
        post.finishLiveCompare(); host.render (520, false, false);
        require (!post.candidateMeasurementIsolated(), "END completes under the same bypass");
    }
}

// A failed BLIND preparation is the trial's failure, not the comparison's.
template<class Require> void verifyCandidateBlindFailure (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "A/B starts");
    host.render (200, true, false);
    require (post.beginLiveBlind() == StartResult::started, "BLIND starts inside A/B");
    LiveTimingFixtureAccess::failBlindPreparation (post, RecoveryReason::gainChanged);
    const auto replaced = host.replacementBlocks;
    host.render (40, true, false);
    require (post.liveBlindStatus().stage == BlindStage::idle && post.liveCompareStatus().active
        && post.liveCompareStatus().verdict == Verdict::accepted && post.candidateCompareActive()
        && !post.candidateReturning() && post.takeCandidateBlindReturned(),
        "A/B LISTEN continues and the listener is told once");
    require (host.replacementBlocks == replaced + 40, "the failed preparation never interrupts PRE2's replacement");
    require (post.outputDecision (output_owner::Activity::recordKeep).refused(), "A/B keeps its capture exclusion");
    require (post.beginLiveBlind() == StartResult::started, "BLIND can be pressed again at once");
    host.render (20, true, false, false);
    require (post.liveBlindStatus().stage == BlindStage::preparing, "the second press prepares a new trial");
    post.finishLiveCompare(); host.render (12, true, false, false); host.render (520, false, false);
    require (!post.candidateMeasurementIsolated(), "END still returns normally");
}

// REAPER's timer names the chain two ticks into every start or seek, after both readers have
// already proved K. That arrival must not re-prove anything: once the requested source has
// arrived it stays, so the listener hears one settled change instead of A, B and the chain in turn.
template<class Require> void verifyCandidateReaperStart (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const bool selectA : { false, true })
    {
        CandidateChainHost api;
        CandidateHost<Require> host (wall, require, 2, 256);
        for (auto& processor : host.instances)
        {
            require (LiveTimingFixtureAccess::configureReaperClock (*processor), "certified REAPER clock fixture");
            processor->getVST3ClientExtensions()->setIHostApplication (&api);
        }
        host.processorTimers = true; host.timerBlocks = 9;
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "REAPER A/B starts");
        post.selectLiveComparePre (selectA);
        host.render (300, true, selectA, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "the chain's token is proven before the seek");
        for (const bool seek : { true, false })
        {
            if (seek) for (auto& clock : host.clocks) clock.offset -= 96000;
            else
            {
                for (auto& clock : host.clocks) clock.playing = false;
                host.render (20, true, selectA, false);
                for (auto& clock : host.clocks) clock.playing = true;
            }
            // POST's input stays B(x) until PRE2's own short re-proof has passed through B.
            host.sourceContinuity.begin();
            if (selectA) host.outputContinuity.begin();
            host.render (32, true, selectA, false);
            if (!selectA) host.outputContinuity.begin();
            host.render (168, true, selectA, false);
            require (host.sourceContinuity.arrived && host.outputContinuity.arrived,
                     "a seek or play settles once, with no return through the serial chain");
            host.sourceContinuity.enabled = host.outputContinuity.enabled = false;
        }
        for (auto& processor : host.instances) processor->getVST3ClientExtensions()->setIHostApplication (nullptr);
    }
}

template<class Require> void verifyCandidateLifetime (Require require)
{
    std::cout << "Candidate lifetime: timer\n" << std::flush;
    verifyCandidateTimerLifetime (require);
    std::cout << "Candidate lifetime: stopped source\n" << std::flush;
    verifyCandidateStoppedSourceReturn (require);
    std::cout << "Candidate lifetime: receiver\n" << std::flush;
    verifyCandidateReceiverLifetime (require);
    std::cout << "Candidate lifetime: exclusion\n" << std::flush;
    verifyCandidateHeldExclusion (require);
    std::cout << "Candidate lifetime: shared PRE2\n" << std::flush;
    verifyCandidateSharedPre2 (require);
    std::cout << "Candidate lifetime: both PREs\n" << std::flush;
    verifyCandidateBothPresRemoved (require);
    std::cout << "Candidate lifetime: bypass fade\n" << std::flush;
    verifyCandidateBypassFade (require);
    std::cout << "Candidate lifetime: BLIND failure\n" << std::flush;
    verifyCandidateBlindFailure (require);
    std::cout << "Candidate lifetime: REAPER start\n" << std::flush;
    verifyCandidateReaperStart (require);
}
}
