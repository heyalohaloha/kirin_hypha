#pragma once
#include "CandidateProductHost.h"
#include "../src/live_compare/LiveCompareOffset.h"
#include <functional>

namespace hypha::tests::align_release_product
{
inline std::function<void()> interruptedAudio;
inline void interruptOnce()
{
    live_compare::beforeAlignFallbackForTest = nullptr;
    interruptedAudio();
}
struct HookScope
{
    ~HookScope() { live_compare::beforeAlignFallbackForTest = nullptr; interruptedAudio = {}; }
};

// A synthetic serial host's real processBlock calls, with NO service/timer/message pump.
// Preparation injects the changed readiness mapping; release itself must come from Audio.
// This is a LISTEN service-entry regression, not a BLIND PCM or measured-estimator proof.
template<class Require> void verify (Require require)
{
    using namespace live_compare;
    HookScope hooks;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512, true);
    host.delayA = host.delayB = 0;
    host.pair();
    auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    require (post.startLiveCompare() == StartResult::started, "release fixture starts real LISTEN");
    post.selectLiveComparePre (true);
    auto callbackOnly = [&]
    {
        wall.advanceBlock (host.block, 48000);
        for (auto& clock : host.clocks) { clock.clock = host.cursor; clock.delay = 0; }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < host.block; ++i)
                host.audio.setSample (ch, i, candidateInput (host.cursor + i, ch));
        host.callback (host.pre1());
        // POST input is independently supplied, never read back from a product ring.
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < host.block; ++i)
                host.audio.setSample (ch, i, candidateInput (host.cursor + i, ch));
        host.callback (post);
        host.cursor += host.block;
    };
    // Establish a proven mapping and enough history without an estimator changing the fixture.
    for (int n = 0; n < 120; ++n)
    {
        host.pre1().serviceLiveCompare(); post.serviceLiveCompare();
        LiveTimingFixtureAccess::deferContentMonitorForTest (post);
        callbackOnly();
    }
    require (post.liveCompareStatus().active && LiveTimingFixtureAccess::alignmentHistoryValid (post),
             "release fixture has active authority and real clock history");
    require (LiveTimingFixtureAccess::confirmAlignmentForTest (post, 1000), "fixture confirms owner estimate");
    callbackOnly();
    require (LiveTimingFixtureAccess::keptAlignmentForTest (post).frames == 1000,
             "real estimate entry retains 1000 in the owner");
    auto& alignment = LiveTimingFixtureAccess::prepareAlignmentForTest (post);
    alignment.readiness.recheck(); alignment.arm();
    for (int n = 0; n < 120 && !alignment.budgetSpent(); ++n) callbackOnly();
    require (alignment.budgetSpent() && alignment.releasedAt() == 0,
             "unanswered same-K budget expires without releasing the retained value");
    callbackOnly(); // latch the spent budget's original K before the injected mapping change
    require (alignment.budgetSpent() && alignment.releasedAt() == 0,
             "one unchanged callback anchors the expired budget before the mapping preparation");
    // Explicit preparation injection: advance readiness away from the real K and back to it.
    // These are not claimed to be two observed host callbacks. No release/token is injected.
    const auto actualK = alignment.readiness.clockK();
    alignment.readiness.observe (true, true, true, actualK - 64);
    alignment.readiness.observe (true, true, true, actualK);
    require (alignment.releasedAt() == 0 && alignment.readiness.pending(),
             "prepared changed mapping remains unreleased until a proven callback");
    int calls = 0;
    interruptedAudio = [&]
    {
        ++calls;
        require (alignment.releasedAt() == 0, "service captured the pre-release state");
        callbackOnly();
        require (alignment.releasedAt() != 0, "real Audio resolve and observed publish release");
        require (post.liveCompareStatus().preAudible && !post.liveCompareStatus().preWaiting,
                 "release callback plays PRE without further waiting");
        LiveTimingFixtureAccess::deferContentMonitorForTest (post);
    };
    beforeAlignFallbackForTest = interruptOnce;
    LiveTimingFixtureAccess::serviceContentTiming (post);
    require (calls == 1, "test processor hook is linked and reached exactly once");
    const auto context = LiveTimingFixtureAccess::alignmentContextForTest (post);
    const bool resend = alignment.requestIsResendForTest (context);
    const auto requested = alignment.requestFor (context);
    require (resend && requested.frames == 1000 && LiveTimingFixtureAccess::keptAlignmentForTest (post).frames == 1000,
             "outer real service actually publishes the now-stale retained resend");
    const auto released = alignment.releasedAt();
    for (int n = 0; n < 2; ++n)
    {
        callbackOnly();
        require (post.liveCompareStatus().verdict == Verdict::accepted && post.liveCompareAlignFrames() == 0
            && alignment.releasedAt() == released && post.liveCompareStatus().preAudible
            && !post.liveCompareStatus().preWaiting, "proven callback rejects stale resend and audibly keeps K");
    }
    require (!alignment.readiness.pending(), "release is consumed even with pending false");
    LiveTimingFixtureAccess::serviceContentTiming (post);
    require (LiveTimingFixtureAccess::keptAlignmentForTest (post).frames == 0
        && post.liveCompareStatus().contentUnmeasured && !post.liveCompareStatus().contentNotRemeasured,
        "next real service consumes release and reports unmeasured");
    callbackOnly();
    require (post.liveCompareAlignFrames() == 0, "owner zero survives the next real callback");
    std::cout << "A3 product service/callback LISTEN: PASS (mapping preparation injected; PCM oracle separate)\n";
}
}
