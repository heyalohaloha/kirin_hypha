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
    host.render (880, false, false, false);
    require (post.candidateMeasurementIsolated() && !post.keepPair(),
             "a long chain keeps delayed comparison input isolated before the overall deadline");
    // The marker would pass only a delay after END and then need the delay again plus settling.
    // The return still has one overall deadline: ten seconds after END, normal measurement resumes.
    host.render (60, false, false, false);
    require (!post.candidateMeasurementIsolated() && !post.candidateReturning(),
             "the overall deadline restores normal measurement even while a long chain is draining");
    // This B's own output stays silent for its first 30 s; measure once it carries the signal.
    host.render (2400, false, false, false);
    host.render (40, false, false); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
    require (measurementActiveAfterCallback (post, [&] { host.render (1, false, false); }), "normal POST measurement resumes");
    host.requireKeep (post, "the real Keep entry is available after return");
}

// A stop restarts the four-second count, so playback in short runs never completes it. The
// return still ends ten seconds after END, also while stopped, without another press. The next
// comparison proves its own clock positions as before; nothing of the old count carries over.
template<class Require> void verifyCandidateReturnDeadline (Require require)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "deadline A/B starts");
    host.render (180, true, false);
    require (host.replacementBlocks > 0, "PRE2 substitutes input before END");
    post.finishLiveCompare(); host.render (20, true, false, false);
    const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
    for (int cycle = 0; cycle < 4; ++cycle) // two seconds of playback and a stop, 9.1 s after END in all
    {
        for (auto& clock : host.clocks) clock.playing = true;
        host.render (188, false, false);
        for (auto& clock : host.clocks) clock.playing = false;
        host.render (20, false, false);
    }
    require (post.candidateMeasurementIsolated() && post.candidateReturning() && state.quietFrames.load() == 0,
             "each stop restarts the four-second count");
    host.render (90, false, false);
    require (!post.candidateMeasurementIsolated() && !post.candidateReturning(),
             "the return ends by its overall deadline while the DAW stays stopped");
    for (auto& clock : host.clocks) clock.playing = true;
    host.render (40, false, false); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
    require (measurementActiveAfterCallback (post, [&] { host.render (1, false, false); }),
             "normal POST measurement resumes in a new window");
    host.requireKeep (post, "Keep starts after the deadline without another press");
    for (int i = 0; i < 24 && !post.isRecording(); ++i)
    { host.render (1, false, false); for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true); }
    post.stopPair(); host.render (20, false, false);
    for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "the next A/B starts");
    host.render (100, true, false);
    require (post.liveCompareStatus().verdict == Verdict::accepted, "the next A/B proves its own clock positions");
}
}
