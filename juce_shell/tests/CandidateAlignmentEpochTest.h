#pragma once
#include "CandidateProductHost.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateAlignmentEpoch (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.delayA = 0; host.delayB = 0; host.pair();
    auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    for (const int reported : { 1545, 0 })
    {
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "candidate starts with the same PRE2");
        host.render (950, true, false, false);
        auto old = post.measureLiveCompareOffset(); old.lagFrames = -1545;
        LiveTimingFixtureAccess::publishMeasuredAlign (post, old);
        host.render (3, true, false, false);
        require (post.liveCompareAlignFrames() == 1545, "the previous session applies its correction");
        post.finishLiveCompare(); host.render (10, true, false, false); host.render (800, false, false);
        host.reportedDelayB = reported;
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "candidate restarts with the same PRE2 after reported compensation changes");
        require (post.liveCompareAlignFrames() == 0, "the prior applied display is absent before the first callback");
        LiveTimingFixtureAccess::publishMeasuredAlign (post, old);
        post.selectLiveComparePre (true); host.render (40, true, true, false);
        require (post.liveCompareAlignFrames() == 0, "a delayed result from the prior session cannot be applied");
        post.finishLiveCompare(); host.render (10, true, false, false); host.render (800, false, false);
    }
}

// Real units differ, so the two sides share a position, not a waveform. The processor's own
// content service confirms A through its normal timer, and BLIND then counts both sources.
template<class Require> void verifyCandidateContentConfirmation (Require require)
{
    using namespace live_compare;
    candidateBroadband = true;
    for (const int late : { 0, 512 }) // 512: a delay nobody compensated leaves A after B
    {
        ProductTestClock wall;
        CandidateHost<Require> host (wall, require, 2, 512);
        host.processorTimers = true; host.timerBlocks = 4;
        host.reportedDelayB = late;
        host.pair(); auto& post = host.post();
        post.setLiveCompareAutoAlign (true);
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "content fixture starts A/B through the real API");
        post.selectLiveComparePre (true);
        bool waited = false;
        for (int i = 0; i < 1200 && (!post.liveCompareStatus().preAudible || post.liveCompareAlignPending()); ++i)
        {
            host.render (1, true, true, false);
            waited = waited || post.liveCompareAlignPending();
        }
        require (waited && post.liveCompareStatus().preAudible && !post.liveCompareAlignPending()
            && post.candidateCompareActive() && post.liveCompareAlignFrames() == 0,
            "A that differs from B is confirmed at its compensated position and then sounds");
        require (post.liveContentTimingObservation().lagFrames == late && !post.candidateAlignUnconfirmed()
            && post.liveCompareStatus().observation != RecoveryReason::preLate,
            "a late A sounds as it is, with the measured amount in place of a wait");
        for (int n = 0; n < 40; ++n)
        {
            host.render (1, true, true, false);
            require (post.liveCompareStatus().preAudible, "confirmed A keeps sounding");
            for (int ch = 0; ch < host.channels; ++ch) for (int i = 0; i < host.block; ++i)
                require (std::abs (host.audio.getSample (ch, i) - candidateA (host.cursor - host.block + i
                    - host.delayA - host.delayB - late, ch)) < 3.0e-6f, "A sounds at its clock position, never B");
        }
        post.selectLiveComparePre (false); host.render (900, true, false, false);
        const auto measured = post.measureLiveCompare();
        require (measured.ok() && post.applyLiveCompareMatch (planMatch (measured, 0.0), MatchChoice::basis),
                 "content fixture matches through the real MATCH");
        host.render (100, true, false, false);
        require (post.beginLiveBlind() == StartResult::started, "content fixture enters BLIND");
        host.render (600, true, false, false);
        require (post.liveBlindStatus().stage == BlindStage::active, "the confirmed comparison reaches an anonymous trial");
        for (const int source : { 1, 2 })
        { require (post.selectLiveBlind (source), "either source can be selected"); host.render (40, true, false, false); }
        require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(),
                 "both sources are counted as heard once content is confirmed");
    }
    candidateBroadband = false;
    // Repeating sines confirm no position. The wait for them ends, and B does not come back.
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.processorTimers = true;
    host.pair(); auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
             "unconfirmed fixture starts A/B through the real API");
    post.selectLiveComparePre (true);
    int blocks = 0;
    while (blocks < 400 && !post.liveCompareStatus().preAudible) { host.render (1, true, true, false); ++blocks; }
    require (blocks < 110 && post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 0,
             "the wait for a position nothing can confirm is short, then A sounds");
    for (int n = 0; n < 500; ++n)
    {
        host.render (1, true, true, false);
        require (post.liveCompareStatus().preAudible, "unconfirmed A keeps sounding; B does not return");
        for (int ch = 0; ch < host.channels; ++ch) for (int i = 0; i < host.block; ++i)
            require (std::abs (host.audio.getSample (ch, i) - candidateA (host.cursor - host.block + i
                - host.delayA - host.delayB, ch)) < 3.0e-6f, "unconfirmed A is A at its clock position");
    }
    require (post.candidateAlignUnconfirmed(), "the footer says what the listener hears");
    post.finishLiveCompare(); host.render (10, true, false, false);
    require (!post.candidateAlignUnconfirmed(), "the notice ends with the comparison");
}
}
