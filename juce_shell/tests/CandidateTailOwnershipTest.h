#pragma once
#include "CandidateProductHost.h"
#include <future>
#include <thread>

namespace hypha::tests
{
// Hold the host's engine lock across a real format replacement while the timer proves a tail.
template<class Require> void verifyCandidateTailOwnership (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.delayB = 30 * 48000;
    host.pair(); auto& post = host.post();
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
             "tail ownership fixture starts A/B");
    host.render (180, true, false, false);
    post.finishLiveCompare(); host.render (20, true, false, false); host.render (950, false, false, false);
    require (post.candidateTailPending(), "tail ownership fixture retains the downstream hold");
    auto& state = LiveTimingFixtureAccess::mutableCandidateStateForTest (post);
    state.quietContext.store (state.returnContext.load());
    state.quietFrames.store (4 * 48000);
    std::promise<void> entered, completed;
    auto entering = entered.get_future(); auto done = completed.get_future();
    bool overlapped;
    std::thread timer;
    {
        const juce::ScopedLock replacement (LiveTimingFixtureAccess::handleOwnership (post));
        timer = std::thread ([&] { entered.set_value(); LiveTimingFixtureAccess::serviceCandidate (post); completed.set_value(); });
        entering.wait();
        overlapped = done.wait_for (std::chrono::milliseconds (250)) == std::future_status::ready;
        prepareFixtureProcessor (post, 44100, host.block);
    }
    timer.join();
    require (!overlapped, "tail release cannot cross engine replacement ownership");
    require (post.candidateTailPending(), "the proof is rechecked after engine replacement");
    state.quietContext.store (state.returnContext.load());
    LiveTimingFixtureAccess::serviceCandidate (post);
    require (!post.candidateTailPending(), "a fresh proof releases the tail");
}
}
