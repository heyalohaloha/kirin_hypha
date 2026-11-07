#pragma once
#include "CandidateLifetimeTest.h"
#include <future>

namespace hypha::tests
{
inline std::function<void()> candidateBlindScopeRaceHook;
inline std::function<void()> candidateBlindRestoreRaceHook;

template<class Require> void verifyCandidateBlindScopeRace (Require require)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    ProductTestClock wall;
    for (const bool candidate : { true, false })
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair(); auto& post = host.post();
        CandidateOtherPair other (host, require);
        juce::MemoryBlock saved; post.getStateInformation (saved);
        if (candidate)
        {
            require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                     "race fixture starts A/B");
            host.render (180, true, false);
            require (host.replacementBlocks > 100, "race follows actual PRE2 replacement");
        }
        else
        {
            require (post.startLiveCompare() == StartResult::started, "race fixture starts ordinary LISTEN");
            host.render (180, false, false);
        }
        const auto scope = LiveTimingFixtureAccess::candidateStateForTest (post).scope;
        std::promise<void> revoked;
        auto notification = revoked.get_future();
        std::thread restore;
        bool notified = false;
        candidateBlindRestoreRaceHook = [&] { revoked.set_value(); };
        candidateBlindScopeRaceHook = [&]
        {
            restore = std::thread ([&] { post.setStateInformation (saved.getData(), static_cast<int> (saved.getSize())); });
            notified = notification.wait_for (std::chrono::seconds (5)) == std::future_status::ready;
        };
        afterLiveBlindRestoreFenceForTest = [] { if (candidateBlindRestoreRaceHook) candidateBlindRestoreRaceHook(); };
        afterLiveBlindScopeAcquiredForTest = [] { if (candidateBlindScopeRaceHook) candidateBlindScopeRaceHook(); };
        const auto result = post.beginLiveBlind();
        afterLiveBlindScopeAcquiredForTest = nullptr;
        if (restore.joinable()) restore.join();
        afterLiveBlindRestoreFenceForTest = nullptr;
        candidateBlindScopeRaceHook = {}; candidateBlindRestoreRaceHook = {};
        require (notified && result == StartResult::notReady, "restore fences BLIND between scope acquisition and final authority");
        const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
        if (candidate)
        {
            require (scope != 0 && state.scope == scope && post.candidateMeasurementIsolated(),
                     "borrowed scope and measurement isolation survive the failed start");
            require (!other.post->keepAll() && !other.post->keepPair(), "another POST cannot record during return");
            host.render (12, true, false, false);
            host.render (100, false, false, false);
            require (post.candidateMeasurementIsolated() && !other.post->keepAll(), "withdrawal does not release the early return");
            host.render (700, false, false);
            require (!post.candidateMeasurementIsolated() && state.scope == 0, "return releases the borrowed scope");
        }
        else host.render (20, false, false);
        for (auto& processor : host.instances) { awaitAnalysisInput (*processor); awaitAnalysisExchange (*processor, true); }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (10);
        bool started = other.post->keepAll();
        while (!started && std::chrono::steady_clock::now() < deadline)
        {
            host.render (1, false, false);
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            started = other.post->keepAll();
        }
        require (started, "failed BLIND eventually releases project capture admission");
        for (int tick = 0; tick < 48 && !other.post->isRecording(); ++tick)
        {
            host.render (1, false, false);
            for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true);
            for (auto* processor : { other.pre.get(), other.post.get() }) awaitAnalysisExchange (*processor, true);
        }
        require (other.post->isRecording(), "All Keep reaches actual Record");
        other.post->stopPair(); post.stopPair(); host.render (20, false, false);
        for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true);
        for (auto* processor : { other.pre.get(), other.post.get() }) awaitAnalysisExchange (*processor, true);
        host.render (100, false, false);
        require (post.beginLiveBlind() == StartResult::started, "failed start left no BLIND controller reservation");
        post.finishLiveCompare(); host.render (20, false, false);
        host.passengers.clear();
    }
}
}
