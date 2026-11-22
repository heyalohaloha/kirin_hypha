#pragma once
#include "LiveTimingFixtureAccess.h"
#include <atomic>
#include <cstdint>
#include <chrono>
#include <functional>
#include <thread>

// Uses real Processor callbacks and a qualified synthetic host. The one paused callback is
// test-only and takes no ring lease. All LISTEN and delay changes run on the message thread.
namespace hypha::tests
{
class LiveMatchSpanLifecycleProductTest
{
public:
    using Processor = KirinHyphaProcessorBase;
    using Phase = hypha::live_compare::StartResult;
    static constexpr float offPulse = 0.96f;
    static constexpr int pulseFrame = 100;

    LiveMatchSpanLifecycleProductTest (bool restart, bool compensation) : restartCase (restart), compensationCase (compensation)
    {
       #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
        if (restartCase)
            hypha::live_compare::afterWholeMatchTokenForTest = [] { if (auto* active = callbackOwner) active->park(); };
       #endif
    }

    ~LiveMatchSpanLifecycleProductTest()
    {
       #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
        if (restartCase) hypha::live_compare::afterWholeMatchTokenForTest = nullptr;
       #endif
    }

    bool active() const noexcept { return restartCase || compensationCase; }
    bool pulseEnabled() const noexcept { return adcOff.load (std::memory_order_acquire); }
    void observePulse (bool injected, float output) noexcept
    {
        if (injected && output == offPulse)
            renderedPulses.fetch_add (1, std::memory_order_release);
    }

    void enterPostCallback() noexcept { if (restartCase) callbackOwner = this; }
    void leavePostCallback() noexcept
    {
        if (! restartCase) return;
        callbackOwner = nullptr;
        if (parked.load (std::memory_order_relaxed))
            returned.store (true, std::memory_order_release);
    }

    void releaseForJoin() noexcept { resumeCallback.store (true, std::memory_order_release); }

    void begin (Processor& post, void (*require) (bool, const char*))
    {
       #ifndef KIRIN_HYPHA_TEST_WALL_CLOCK
        require (! restartCase, "W1 requires the production callback test hook");
       #endif
        post.setWholeMatchPreferred (true);
        require (post.startLiveCompare() == Phase::started, "product starts ALL PLAYED LISTEN");
        post.selectLiveComparePre (false);
        require (LiveTimingFixtureAccess::wholeSpanEnabledForTest (post), "ALL PLAYED has a live queue");
    }

    template<class Pause, class Resume>
    bool tick (Processor& post, Pause&& pauseAudio, Resume&& resumeAudio, void (*require) (bool, const char*))
    {
        if (restartCase) return tickRestart (post, require);
        using namespace hypha::live_compare;
        constexpr std::uint64_t sampleRate = 48000, blockSize = 512;
        post.serviceLiveMatchSpan();
        const auto published = LiveTimingFixtureAccess::wholeSpanPublishedForTest (post);
        const auto consumed = LiveTimingFixtureAccess::wholeSpanConsumedForTest (post);
        const auto status = post.liveCompareStatus();
        switch (state)
        {
            case 0:
            {
                if (consumed < sampleRate * 3 || status.preSelected) return false;
                if (!noticeDuringReadChecked)
                {
                    post.selectLiveComparePre(true);
                    state = 10;
                    return false;
                }
                pauseAudio();
                const auto prior = post.measureLiveCompare (true);
                if (! prior.ok()) { resumeAudio(); return false; }
                require (prior.wholeOwnerBound && prior.proofBound && prior.analysisUnits >= sampleRate * 3,
                         "at least three seconds of linear playback create a genuine old ALL PLAYED plan");
                oldPlan = planMatch (prior, 0.0);
                require (oldPlan.failure == MatchFailure::none && prior.compensationProofBound
                         && oldPlan.compensationProofBound && oldPlan.compensationProof == prior.compensationProof,
                         "the pre-switch MATCH plan carries its actual compensation generation");
                const auto before = post.liveCompareStatus();
                lastBeforeOff = LiveTimingFixtureAccess::wholeSpanPublishedForTest (post);
                readBeforeOff = LiveTimingFixtureAccess::wholeSpanConsumedForTest (post);
                post.kirinHostDelayCompensationStateChanged (false);
                require (! post.measureLiveCompare (true).ok()
                         && post.applyLiveCompareMatch (oldPlan, choice (oldPlan)).failure == MatchFailure::stale,
                         "compensation OFF rejects the old proof immediately, before the next POST callback");
                post.kirinHostDelayCompensationStateChanged (true);
                require (! post.measureLiveCompare (true).ok()
                         && post.applyLiveCompareMatch (oldPlan, choice (oldPlan)).failure == MatchFailure::stale,
                         "OFF then ON without a POST callback cannot renew the previous proof");
                const auto after = post.liveCompareStatus();
                require (before.gain == after.gain && before.postTarget == after.postTarget && before.matched == after.matched,
                         "no stale attempt changes either approved gain");
                post.kirinHostDelayCompensationStateChanged (false);
                adcOff.store (true, std::memory_order_release);
                resumeAudio();
                state = 1;
                return false;
            }
            case 10:
                if (!status.preSelected || !status.preAudible) return false;
                pauseAudio();
                verifyOffDuringRead(post, pauseAudio, resumeAudio, require);
                post.kirinHostDelayCompensationStateChanged(true);
                post.selectLiveComparePre(false);
                adcOff.store(false, std::memory_order_release);
                renderedPulses.store(0, std::memory_order_release);
                noticeDuringReadChecked = true;
                state = 0;
                resumeAudio();
                return false;
            case 1:
                if (renderedPulses.load (std::memory_order_acquire) * blockSize < sampleRate * 2)
                    return false;
                pauseAudio();
                post.serviceLiveMatchSpan();
                require (post.liveCompareStatus().compensationOff && ! post.liveCompareStatus().preSelected,
                         "compensation is really OFF during the selected-POST probe");
                require (LiveTimingFixtureAccess::wholeSpanPublishedForTest (post) == lastBeforeOff
                         && LiveTimingFixtureAccess::wholeSpanConsumedForTest (post) == readBeforeOff,
                         "two seconds with compensation OFF never publish or feed evidence");
                adcOff.store (false, std::memory_order_release);
                post.kirinHostDelayCompensationStateChanged (true);
                resumeAudio();
                state = 2;
                return false;
            case 2:
                if (status.compensationOff || published <= lastBeforeOff || consumed <= readBeforeOff)
                    return false;
                pauseAudio();
                if (const auto measured = post.measureLiveCompare (true); measured.ok())
                {
                    require (measured.analysisUnits > readBeforeOff && measured.postPeakDbtp < -10.0,
                             "fresh linear proof resumes ALL PLAYED without integrating OFF-only POST peaks");
                    const auto fresh = planMatch (measured, 0.0);
                    require (fresh.compensationProofBound && fresh.compensationProof == measured.compensationProof
                             && fresh.compensationProof != oldPlan.compensationProof,
                             "the new plan carries the distinct compensation generation proved by POST");
                    require (post.applyLiveCompareMatch (oldPlan, choice (oldPlan)).failure == MatchFailure::stale,
                             "new corresponding blocks do not make the old plan reusable");
                    require (post.applyLiveCompareMatch (fresh, choice (fresh)),
                             "ON with new proof applies the freshly measured plan");
                    freshApproved = fresh;
                    state = 3;
                }
                resumeAudio();
                return false;
            default:
                if (! status.matchReady) return false;
                require (status.matched, "POST acknowledges the newly approved MATCH on a real callback");
                pauseAudio();
                const auto beforeStale = post.liveCompareStatus();
                const auto revision = LiveTimingFixtureAccess::gainRevision(post).load();
                const auto receipt = LiveTimingFixtureAccess::gainReceipt(post);
                require(beforeStale.matchReady && receipt == revision, "fresh MATCH is applied before stale-plan probe");
                post.kirinHostDelayCompensationStateChanged(false);
                post.kirinHostDelayCompensationStateChanged(true); // deliberately no callback before stale approval
                require(post.applyLiveCompareMatch(freshApproved, choice(freshApproved)).failure == MatchFailure::stale,
                    "a previously applied plan is stale after OFF then ON");
                require(LiveTimingFixtureAccess::gainRevision(post).load() == revision
                    && LiveTimingFixtureAccess::gainReceipt(post) == receipt
                    && post.liveCompareStatus().matchReady == beforeStale.matchReady,
                    "rejecting a stale compensation proof preserves the applied revision, its receipt and readiness");
                resumeAudio();
                return true;
        }
    }

private:
    template<class Pause, class Resume>
    void verifyOffDuringRead(Processor& post, Pause& pauseAudio, Resume& resumeAudio,
                             void (*require)(bool,const char*))
    {
      #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
        using namespace hypha::live_compare;
        using Clock = std::chrono::steady_clock;
        std::atomic<bool> noticeDone {false};
        std::thread notifyingHost;
        bool observedOff = false;
        const auto published = LiveTimingFixtureAccess::wholeSpanPublishedForTest(post);
        const auto beforeOff = renderedPulses.load(std::memory_order_acquire);
        std::function<void()> hook = [&] {
            notifyingHost = std::thread([&] {
                post.kirinHostDelayCompensationStateChanged(false);
                noticeDone.store(true,std::memory_order_release);
            });
            auto deadline = Clock::now() + std::chrono::milliseconds(1000);
            while (!noticeDone.load(std::memory_order_acquire) && Clock::now() < deadline) std::this_thread::yield();
            if (!noticeDone.load(std::memory_order_acquire)) return; // don't join under the old measurement lock
            adcOff.store(true,std::memory_order_release);
            resumeAudio();
            deadline = Clock::now() + std::chrono::milliseconds(1000);
            while (renderedPulses.load(std::memory_order_acquire) == beforeOff && Clock::now() < deadline)
                std::this_thread::yield();
            pauseAudio();
            const auto after = post.liveCompareStatus();
            observedOff = renderedPulses.load(std::memory_order_acquire) > beforeOff
                && after.compensationOff && after.preSelected && !after.preAudible
                && LiveTimingFixtureAccess::wholeSpanPublishedForTest(post) == published;
        };
        onPausedMatchRead = &hook;
        duringLiveMatchReadLeaseForTest = [] { if (onPausedMatchRead) (*onPausedMatchRead)(); };
        const auto measurement = post.measureLiveCompare(true);
        duringLiveMatchReadLeaseForTest = nullptr; onPausedMatchRead = nullptr;
        if (notifyingHost.joinable()) notifyingHost.join();
        require(observedOff && !measurement.ok(),
            "while MATCH is still halfway through a counted read, a new POST callback sees the separate OFF notification, outputs POST and adds no evidence");
      #else
        (void)post; (void)pauseAudio; (void)resumeAudio;
        require(false,"AA1 requires the MATCH read boundary hook");
      #endif
    }

    bool noticeDuringReadChecked = false;
    inline static thread_local std::function<void()>* onPausedMatchRead = nullptr;

    static hypha::live_compare::MatchChoice choice (const hypha::live_compare::MatchPlan& plan) noexcept
    {
        using hypha::live_compare::MatchChoice;
        return plan.needsApproval ? MatchChoice::lowerPost : MatchChoice::basis;
    }

    bool tickRestart (Processor& post, void (*require) (bool, const char*))
    {
        if (state == 0 && LiveTimingFixtureAccess::wholeSpanPublishedForTest (post) != 0)
        {
            listenSession = post.liveCompareListenSession();
            armed.store (true, std::memory_order_release);
            state = 1;
        }
        if (state == 1 && parked.load (std::memory_order_acquire))
        {
            post.stopLiveCompare();
            post.setWholeMatchPreferred (false);
            require (post.startLiveCompare() == Phase::started, "while old POST is parked, real LAST 4 s LISTEN replaces ALL PLAYED");
            require (post.liveCompareListenSession() != listenSession && post.liveCompareStatus().active
                     && ! LiveTimingFixtureAccess::wholeSpanEnabledForTest (post)
                     && LiveTimingFixtureAccess::wholeSpanPublishedForTest (post) == 0,
                     "the replacement removed the accumulator before the old callback gets its lease");
            resumeCallback.store (true, std::memory_order_release);
            state = 2;
        }
        if (state != 2 || ! returned.load (std::memory_order_acquire)) return false;
        require (post.liveCompareStatus().active && LiveTimingFixtureAccess::wholeSpanPublishedForTest (post) == 0
                 && LiveTimingFixtureAccess::wholeSpanConsumedForTest (post) == 0,
                 "the resumed real POST callback does not publish stale ALL PLAYED data");
        return true;
    }

    void park() noexcept
    {
        if (! armed.exchange (false, std::memory_order_acq_rel)) return;
        parked.store (true, std::memory_order_release);
        while (! resumeCallback.load (std::memory_order_acquire)) std::this_thread::yield();
    }

    const bool restartCase, compensationCase;
    int state = 0; // message thread only
    hypha::live_compare::MatchPlan oldPlan;
    hypha::live_compare::MatchPlan freshApproved {};
    std::uint64_t listenSession = 0, lastBeforeOff = 0, readBeforeOff = 0;
    std::atomic<bool> armed { false }, parked { false }, resumeCallback { false }, returned { false }, adcOff { false };
    std::atomic<std::uint64_t> renderedPulses { 0 };
    static inline thread_local LiveMatchSpanLifecycleProductTest* callbackOwner = nullptr;
};
}
