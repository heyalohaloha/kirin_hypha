#pragma once
#include "CandidateProductHost.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateLongReturn (Require require)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.delayA = 0; host.delayB = 30 * 48000; host.pair();
    auto& post = host.post();
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
             "long-delay candidate starts through the product entry");
    host.render (180, true, false, false);
    require (host.replacementBlocks > 0 && post.liveCompareStatus().verdict != Verdict::accepted,
             "PRE2 actually substitutes input before POST can accept out-of-ring PCM");
    post.finishLiveCompare(); host.render (20, true, false, false);
    host.render (2600, false, false, false);
    require (post.candidateMeasurementIsolated() && !post.keepPair(),
             "past ring capacity plus settling, delayed comparison input remains isolated");
    host.render (300, false, false, false);
    require (post.candidateMeasurementIsolated() && !post.keepPair(),
             "the return marker cannot release the scope before four seconds of settling");
    const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
    const auto* observer = state.returnObserver.control();
    require (observer && observer->proofAvailable,
             "the independent return clock can verify a delay longer than the PCM ring");
    // The marker is taken once that clock is proven, a delay after END. It then needs the delay
    // again plus settling: the wait is long only because this comparison input really is delayed.
    host.render (3000, false, false, false);
    require (!post.candidateMeasurementIsolated() && !post.candidateReturning(),
             "the passed marker and four seconds of processing restore normal measurement");
    host.render (40, false, false); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
    require (measurementActiveAfterCallback (post, [&] { host.render (1, false, false); }), "normal POST measurement resumes");
    host.requireKeep (post, "the real Keep entry is available after return");
}
}
