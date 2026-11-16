#pragma once
#include "CandidateProductHost.h"

namespace hypha::tests
{
#if defined (KIRIN_HYPHA_TEST_WALL_CLOCK)
inline int tailEstimateCalls = 0;
#endif
template<class Require> void verifyTailSurvivesPairChange (Require require)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.delayA = 0; host.delayB = 12 * 48000 + 1920; host.reportedDelayB = -1920;
    host.processorTimers = true; host.timerBlocks = 4; candidateBroadband = true;
    host.pair(); auto& post = host.post(); post.setLiveCompareAutoAlign (true);
    host.render (1200, false, false, false);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "A/B starts");
    host.render (180, true, false, false);
    require (host.replacementBlocks > 0, "the candidate substituted audio");
    post.finishLiveCompare(); host.render (20, true, false, false); host.render (930, false, false, false);
    require (post.candidateTailPending(), "the deadline leaves a tail");
    const auto oldTicket = post.restoreOrPairTicket();
    require (post.setPairCandidate (host.pre2().instanceId(), "Source"), "select a different PRE");
    host.pre1Enabled = false; // the selected PRE is no evidence that the original input drained
    host.render (10, false, false, false);
    require (post.candidateTailPending() && ! post.keepPair() && post.keepPhase() == KIRIN_KEEP_PHASE_IDLE,
             "a pair change cannot clear downstream audio or start Keep");
    require (post.startLiveCompare() == StartResult::started, "LISTEN continues on the new pair");
    post.selectLiveComparePre (true);
   #if defined (KIRIN_HYPHA_TEST_WALL_CLOCK)
    tailEstimateCalls = 0; beforeContentEstimateForTest = [] { ++tailEstimateCalls; };
   #endif
    host.render (1200, false, true, false); // allow the newly demanded PRE run to cover B's delay
    require (post.liveCompareStatus().verdict == Verdict::accepted, "the new pair has a proven playback path");
   #if defined (KIRIN_HYPHA_TEST_WALL_CLOCK)
    require (tailEstimateCalls == 0, "the new pair does not learn from remaining comparison audio");
   #endif
    require (post.candidateTailPending() && post.liveCompareAlignFrames() == 0,
             "the new pair retains the tail and learns no correction");
    host.pre1Enabled = true;
    int blocks = 0;
    // A restarted PRE first fills B's delay, then a fresh marker traverses it and settles.
    for (; blocks < 4000 && post.candidateTailPending(); ++blocks) host.render (1, false, true, false);
    require (!post.candidateTailPending(), "the original path's marker and four settled seconds clear the tail");
    require (LiveTimingFixtureAccess::candidateStateForTest (post).quietFrames.load() >= 4 * 48000,
             "the original path's clock-proven count reaches four seconds");
    host.render (200, false, true, false);
   #if defined (KIRIN_HYPHA_TEST_WALL_CLOCK)
    beforeContentEstimateForTest = nullptr;
    require (tailEstimateCalls > 0, "content monitoring resumes only after the tail clears");
   #endif
    post.finishLiveCompare(); host.render (40, false, false, false);
    require (!post.keepPair (oldTicket), "the old pair's waiting press stays cancelled after drainage");
    host.requireKeep (post, "a fresh Keep starts on the new pair after drainage");
    candidateBroadband = false;
}
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
    // INV-AB6: B still plays the comparison's audio for its 30 s. Measurement and LISTEN go on, but
    // nothing of it is kept: Keep starts only once an ordinary marker has passed the chain and settled.
    const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
    require (state.tail.load() != 0 && ! post.keepPair() && ! post.keepAll() && post.keepPhase() == KIRIN_KEEP_PHASE_IDLE
             && ! post.isRecording(), "Keep does not start while B may still carry the comparison's audio");
    host.render (1400, false, false, false); // 25.5 s after END: the comparison's audio is still in B
    require (state.tail.load() != 0 && ! post.keepPair() && post.keepPhase() == KIRIN_KEEP_PHASE_IDLE,
             "Keep keeps waiting for the whole of B's delay");
    // The marker needs PRE's run to reach 30 s back before it counts, then passes the chain's 30 s.
    int blocks = 0;
    for (; blocks < 7000 && state.tail.load() != 0; ++blocks) host.render (1, false, false, false);
    std::printf ("A/B deadline tail: Record and Keep are accepted %.1f s after END\n", 25.5 + blocks * 512.0 / 48000.0);
    require (state.tail.load() == 0, "an ordinary marker passes the chain and settles");
    // This B's own output stays silent for its first 30 s; measure once it carries the signal.
    host.render (40, false, false); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
    require (measurementActiveAfterCallback (post, [&] { host.render (1, false, false); }), "normal POST measurement resumes");
    host.requireKeep (post, "the real Keep entry is available once the ordinary path is proven");
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
    // The deadline alone proved nothing about B's input: Keep waits for the ordinary path (INV-AB6).
    require (state.tail.load() != 0 && ! post.keepPair() && post.keepPhase() == KIRIN_KEEP_PHASE_IDLE,
             "Keep does not start before the ordinary path is proven");
    int settle = 0;
    for (; settle < 800 && state.tail.load() != 0; ++settle) host.render (1, false, false);
    require (state.tail.load() == 0 && settle > 300, "four seconds of ordinary playback end the wait");
    host.render (40, false, false); awaitAnalysisInput (post); awaitAnalysisExchange (post, true);
    host.requireKeep (post, "Keep starts once the ordinary path is proven");
    for (int i = 0; i < 24 && !post.isRecording(); ++i)
    { host.render (1, false, false); for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true); }
    post.stopPair(); host.render (20, false, false);
    for (auto& processor : host.instances) awaitAnalysisExchange (*processor, true);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "the next A/B starts");
    host.render (100, true, false);
    require (post.liveCompareStatus().verdict == Verdict::accepted, "the next A/B proves its own clock positions");
}
// INV-AB6 with automatic alignment on: the deadline ends the return while a long B still carries
// the comparison's audio (12 s reported, 40 ms unreported). The clocks and a strong content match
// cannot tell that audio from the ordinary path, so an ordinary LISTEN started then learns nothing
// from it and keeps nothing: PRE plays at K. Once an ordinary marker has passed the chain and
// settled for four seconds, the content is learnt as in any LISTEN.
template<class Require> void verifyCandidateDeadlineTail (Require require)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    constexpr int unreported = 1920;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.processorTimers = true; host.timerBlocks = 4;
    host.delayA = 0; host.delayB = 12 * 48000 + unreported; host.reportedDelayB = -unreported;
    candidateBroadband = true;
    host.pair(); auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    // Another reader of PRE1 (a POST of the same pair's analysis) keeps its PCM run going throughout,
    // so the LISTEN below can read PRE 12 s back from its first block.
    SharedRingMapping reader;
    require (reader.open (pairKeyForPreInstance (host.pre1().instanceId().toStdString()), 48000), "another reader demands PRE1's PCM");
    host.render (1200, false, false, false);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "the long-B candidate starts");
    host.render (180, true, false, false);
    require (host.replacementBlocks > 0, "PRE2 substitutes input before END");
    post.finishLiveCompare(); host.render (20, true, false, false);
    host.render (930, false, false, false); // 10.1 s after END
    require (! post.candidateMeasurementIsolated() && ! post.candidateReturning(), "the deadline ends the return");
    require (post.startLiveCompare() == StartResult::started, "an ordinary LISTEN starts after the deadline");
    post.selectLiveComparePre (true);
    int learnt = 0, audible = 0, told = 0;
    for (int n = 0; n < 150; ++n) // until 11.7 s after END: B still plays the comparison's audio
    {
        host.render (1, false, true, false);
        const auto s = post.liveCompareStatus();
        learnt += post.liveCompareAlignFrames() != 0 ? 1 : 0;
        audible += s.preAudible ? 1 : 0; told += s.contentUnmeasured ? 1 : 0;
    }
    std::printf ("A/B deadline tail: LISTEN blocks with a learnt correction %d, PRE audible %d, told unmeasured %d\n", learnt, audible, told);
    require (learnt == 0, "nothing is learnt for PRE's alignment from B's remaining comparison audio");
    require (audible > 30 && told > 0, "PRE plays at its clock position, told as unmeasured");
    int blocks = 150;
    for (; blocks < 1500 && post.liveCompareAlignFrames() != unreported; ++blocks) host.render (1, false, true, false);
    std::printf ("A/B deadline tail: the ordinary path is learnt %.1f s after the LISTEN began\n", blocks * 512.0 / 48000.0);
    require (post.liveCompareAlignFrames() == unreported && blocks > 500,
             "the ordinary path is learnt only after its marker passed the chain and settled");
    post.finishLiveCompare(); host.render (40, false, false, false);
    candidateBroadband = false;
}
// A VST3 host that wraps a delayed insert's position the reported delay before the loop start, in a
// loop shorter than the four settling seconds: the return ends at its deadline while stopped, and the
// tail's watch then counts across the wraps the clock proof corroborates, so it ends after four seconds
// of looped playback. A seek still restarts the count.
template<class Require> void verifyCandidateShortLoopTail (Require require)
{
    using namespace live_compare;
    ValidationStorageSandbox isolated;
    writeCandidateFixtureIdentity (require, isolated.directory());
    constexpr std::int64_t loopStart = 64 * 24000, length = 96000; // two seconds
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.delayA = 1024; host.delayB = 3072;
    for (std::size_t side = 0; side < host.instances.size(); ++side)
    {
        require (LiveTimingFixtureAccess::configureStudioProClock (*host.instances[side]), "the certified VST3 content clock");
        auto& clock = host.clocks[side];
        clock.compensatedContent = clock.tailBeforeStart = true;
        clock.outputPresentation = side == 0 ? host.delayA + host.delayB : side == 1 ? host.delayB : 0;
        clock.offset = clock.loopStart = loopStart; clock.loopLength = length;
    }
    host.pair(); auto& post = host.post();
    host.render (400, false, false, false);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "A/B starts in the short loop");
    host.render (200, true, false, false);
    post.finishLiveCompare(); host.render (20, true, false, false);
    for (auto& clock : host.clocks) clock.playing = false;
    host.render (960, false, false, false); // 10.2 s after END, stopped: only the deadline ends the return
    const auto& state = LiveTimingFixtureAccess::candidateStateForTest (post);
    require (! post.candidateReturning() && state.tail.load() != 0, "the deadline ends the return and leaves the tail");
    require (post.setPairCandidate (host.pre2().instanceId(), "Source"), "select the other PRE during the tail");
    host.render (4, false, false, false);
    require (state.tail.load() != 0, "changing pair does not erase the loop's tail");
    for (auto& clock : host.clocks) clock.playing = true;
    int blocks = 0, wraps = 0;
    for (; blocks < 1500 && state.tail.load() != 0; ++blocks) // up to 16 s of looped playback
    {
        const auto before = host.clocks[2].getPosition()->getTimeInSamples();
        host.render (1, false, false, false);
        wraps += *host.clocks[2].getPosition()->getTimeInSamples() < *before ? 1 : 0;
    }
    std::printf ("A/B deadline tail in a 2 s loop: the watch ended after %.1f s of playback and %d wraps\n", blocks * 512.0 / 48000.0, wraps);
    require (state.tail.load() == 0 && wraps >= 2, "the tail's watch ends across the wraps of a short loop");
}
}
