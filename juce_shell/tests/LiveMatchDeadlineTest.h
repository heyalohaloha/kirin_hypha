#pragma once
#include "CandidateProductHost.h"

namespace hypha::tests
{
// A MATCH that cannot measure (here: silence) does not try again forever. Ten seconds after the
// press it ends with its reason, and LISTEN goes on at the levels it had, without another press.
template<class Require> void verifyLiveMatchDeadline (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    require (post.startLiveCompare() == StartResult::started, "LISTEN starts before an unmeasurable MATCH");
    host.render (100, false, false);
    host.inputScale = 0.0f;
    host.render (400, false, false);
    post.requestLiveCompareMatch();
    MatchPlan plan; bool applied = false;
    host.render (880, false, false);
    require (! post.takeLiveCompareMatch (plan, applied), "MATCH keeps measuring within its deadline");
    host.render (60, false, false);
    require (post.takeLiveCompareMatch (plan, applied) && ! applied && plan.failure != MatchFailure::none,
             "ten seconds after the press MATCH ends with its reason");
    const auto status = post.liveCompareStatus();
    require (status.active && ! status.matched && ! status.signalWait, "LISTEN goes on without MATCH");
    host.render (100, false, false);
    require (! post.takeLiveCompareMatch (plan, applied), "the ended MATCH does not measure again by itself");
}
}
