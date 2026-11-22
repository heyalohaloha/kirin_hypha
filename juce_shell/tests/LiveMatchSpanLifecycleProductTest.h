#pragma once
#include "LiveTimingFixtureAccess.h"
#include <atomic>
#include <cstdint>
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
        constexpr std::uint64_t sampleRate = 48000, blockSize = 512;
        post.serviceLiveMatchSpan();
        const auto published = LiveTimingFixtureAccess::wholeSpanPublishedForTest (post);
        const auto consumed = LiveTimingFixtureAccess::wholeSpanConsumedForTest (post);
        const auto status = post.liveCompareStatus();
        switch (state)
        {
            case 0:
                if (consumed < sampleRate || status.preSelected) return false;
                pauseAudio();
                post.serviceLiveMatchSpan();
                lastBeforeOff = LiveTimingFixtureAccess::wholeSpanPublishedForTest (post);
                readBeforeOff = LiveTimingFixtureAccess::wholeSpanConsumedForTest (post);
                post.kirinHostDelayCompensationStateChanged (false);
                adcOff.store (true, std::memory_order_release);
                resumeAudio();
                state = 1;
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
            default:
                if (status.compensationOff || published <= lastBeforeOff || consumed < sampleRate * 3)
                    return false;
                if (const auto measured = post.measureLiveCompare (true); measured.ok())
                {
                    require (measured.analysisUnits >= sampleRate * 3 && measured.postPeakDbtp < -10.0,
                             "fresh proof resumes ALL PLAYED, but OFF-only near-full-scale POST peaks never enter it");
                    return true;
                }
                return false;
        }
    }

private:
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
    std::uint64_t listenSession = 0, lastBeforeOff = 0, readBeforeOff = 0;
    std::atomic<bool> armed { false }, parked { false }, resumeCallback { false }, returned { false }, adcOff { false };
    std::atomic<std::uint64_t> renderedPulses { 0 };
    static inline thread_local LiveMatchSpanLifecycleProductTest* callbackOwner = nullptr;
};
}
