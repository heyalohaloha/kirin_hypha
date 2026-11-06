#pragma once
#include "AaxChunkProductFixture.h"
#include "AlignContextProductTest.h"

namespace aax_analysis_product
{
// Deterministic publication races: let the real estimator complete, then let a host thread
// restore or re-prepare before publication. Also inject the tuple after the non-RT check to
// exercise the independent RT rejection, through actual processBlock callbacks at the same K.
inline void staleAlignmentCannotCrossContext (int channels)
{
    using namespace hypha::live_compare;
    for (bool restore : { false, true })
    {
        ChunkedPost chain (channels);
        auto& post = *chain.post;
        chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select the race fixture PRE");
        require (post.startLiveCompare() == StartResult::started, "race fixture LISTEN starts");
        post.selectLiveComparePre (true);
        chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "race fixture obtains its own alignment");
        const auto estimate = post.measureLiveCompareOffset();
        require (offsetCanVote (estimate) && estimate.lagFrames == -1024, "race starts with a real strong estimate");
        OffsetMonitor monitor;
        monitor.observe (estimate, false);
        const auto step = monitor.observe (estimate, false);
        juce::MemoryBlock saved;
        post.getStateInformation (saved);
        std::thread host ([&] {
            if (restore) post.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            else
            {
                post.prepareToPlay (44100, ChunkedPost::chunkFrames);
                post.prepareToPlay (48000, ChunkedPost::chunkFrames);
            }
        });
        host.join();
        require (! post.alignLiveCompare (step, estimate), "a completed old estimate cannot publish into the new context");
        LiveTimingFixtureAccess::configureAaxClock (post);
        if (! restore) LiveTimingFixtureAccess::enableFixtureWrites (post);
        chain.chunk (false); // RT observes the revoked output before a new explicit LISTEN
        // Service retires the old permission; starting again is an explicit user action.
        LiveTimingFixtureAccess::serviceTiming (post);
        require (post.startLiveCompare() == StartResult::started, "the same pair can start in the new context");
        post.selectLiveComparePre (true);
        LiveTimingFixtureAccess::publishMeasuredAlign (post, estimate);
        for (int i = 0; i < 40; ++i) chain.chunk (false);
        require (post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 0,
                 "RT rejects stale alignment even at the same K before another service");
        chain.postScale = 0.0f;
        chain.run (220);
        require (post.liveCompareAlignFrames() == 0, "undetermined content cannot revive the stale frame count");
        chain.clean ("restore and format races never play a stale address");
    }
}

inline void chunkedPost (int channels)
{
    staleAlignmentCannotCrossContext (channels);
    alignmentRejectsChangingFormat (channels);
    alignmentBelongsToPreOwner (channels);
    using hypha::live_compare::BlindStage;
    using hypha::live_compare::Verdict;
    {
        ChunkedPost initial (channels);
        initial.seek (5000); initial.enterLoop (48000);
        initial.until (200, [&] { return initial.post->setPairCandidate (initial.pre->instanceId(), {}); }, "select PRE inside a loop");
        require (initial.post->startLiveCompare() == hypha::live_compare::StartResult::started, "LISTEN starts inside a loop");
        initial.post->selectLiveComparePre (true);
        initial.settled (160, "negative K proves itself from an initial loop");
        initial.until (400, [&] { return initial.post->liveCompareAlignFrames() == 1024; }, "initial loop also aligns the audio");
        initial.run (100);
        initial.clean ("an initial loop never played a wrong frame");
        initial.post->finishLiveCompare();
        initial.run (4);
    }
    ChunkedPost chain (channels);
    auto& post = *chain.post;
    constexpr int lap = 47; // chunks in a 48000-frame loop
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "exact PRE is selected");
    require (post.startLiveCompare() == hypha::live_compare::StartResult::started, "LISTEN starts with POST at the hardware buffer");
    post.selectLiveComparePre (true);
    chain.settled (40, "LISTEN plays PRE through the position the host did not report");
    chain.run (20);
    require (chain.exact >= 80, "every POST callback plays the PRE at clock minus K");
    chain.until (400, [&] { return post.measureLiveCompareOffset().determined; }, "the estimate sees the audio arrive late");
    require (post.measureLiveCompareOffset().lagFrames == -1024, "PRE is 21.33 ms early through K");
    chain.clean ("LISTEN never played a wrong frame");

    // PRE early by a settled amount is read that much later, with no setting anywhere.
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "the settled offset is aligned");
    chain.settled (10, "PRE returns on the aligned address");
    require (post.measureLiveCompareOffset().lagFrames == -1024 && post.liveCompareStatus().preSelected,
             "K and the estimate stay what they were");
    hypha::tests::prepareFixtureProcessor (post, 48000, ChunkedPost::postFrames);
    LiveTimingFixtureAccess::configureAaxClock (post);
    for (int i = 0; i < 40; ++i) chain.chunk (false);
    require (post.liveCompareAlignFrames() == 1024 && post.liveCompareStatus().preAudible,
             "same-format prepare keeps alignment before any fresh content estimate");
    chain.seek (40000);
    chain.settled (40, "PRE returns after a seek");
    require (post.liveCompareAlignFrames() == 1024, "a seek keeps the value");
    post.setLiveCompareAutoAlign (false);
    chain.until (10, [&] { return post.liveCompareAlignFrames() == 0; }, "off reads PRE at K alone");
    chain.settled (10, "PRE keeps sounding with it off");
    post.setLiveCompareAutoAlign (true);
    chain.until (200, [&] { return post.liveCompareAlignFrames() == 1024; }, "on again aligns it again");
    post.finishLiveCompare();
    chain.run (4);
    require (post.setPairCandidate (chain.pre->instanceId(), {}), "an explicit pair change is accepted");
    require (post.startLiveCompare() == hypha::live_compare::StartResult::started, "start immediately after the pair change");
    post.selectLiveComparePre (true);
    for (int i = 0; i < 30; ++i) chain.chunk (false);
    require (post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 0,
             "a pair change discards alignment before the next service timer");
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "the new pair measures its own alignment");
    chain.contentK = 1536;
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 2048; }, "a changed delay is aligned again");
    post.setLiveCompareAutoAlign (false);
    chain.until (10, [&] { return post.liveCompareAlignFrames() == 0; }, "off removes the updated alignment");
    chain.run (400);
    require (post.liveCompareStatus().preAudible && ! post.liveCompareStatus().contentHeld,
             "off establishes today's offset baseline without holding PRE for a past change");
    post.setLiveCompareAutoAlign (true);
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 2048; }, "the current offset aligns again");
    chain.postScale = 0.0f;
    chain.run (220);
    require (post.liveCompareAlignFrames() == 2048 && ! post.liveCompareStatus().contentHeld,
             "silence neither discards the value nor holds the comparison");
    chain.postScale = 0.5f;
    chain.contentK = -512;
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 0 && post.liveCompareStatus().preAudible; },
                 "a removed delay returns to K alone without a content hold");
    chain.contentK = 512;
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "the early PRE aligns again");
    chain.clean ("alignment never played a wrong frame");

    // H/W Insert Delay 21.33 ms from zero: the host moves the position and K is proven afresh.
    // Keep the content delay until new content measurements establish that it is no longer needed.
    const auto delayed = chain.withheld;
    chain.setDelay (512);
    chain.settled (40, "PRE returns on the new K");
    require (chain.withheld > delayed && post.liveCompareAlignFrames() == 1024, "new K proof keeps the content delay");
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 0 && post.measureLiveCompareOffset().determined
                                   && post.measureLiveCompareOffset().lagFrames == 0; }, "fresh content removes the unnecessary correction");
    chain.clean ("the delay change never played the old K");

    // A seek: the selection stays, K proves itself again. The position is chosen so that the
    // last callback of the chunk that crosses the loop end below starts past it.
    const auto seeked = chain.withheld;
    chain.seek (20200 - 16 * ChunkedPost::chunkFrames);
    chain.settled (40, "PRE returns after a seek without another press");
    require (chain.withheld > seeked && post.liveCompareStatus().preSelected, "the seek withheld PRE and kept the selection");
    chain.clean ("the seek never played a wrong frame");

    // Stop and play: the first chunk after play passes as the host reported it, then K proves
    // itself again and the same selection returns.
    chain.setPlaying (false);
    chain.run (6);
    require (chain.exact == 0 && post.liveCompareStatus().preSelected, "a stop is POST with the selection kept");
    chain.setPlaying (true);
    chain.seek (20200 - 16 * ChunkedPost::chunkFrames);
    chain.settled (40, "PRE returns after play without another press");
    chain.clean ("stop and play never played a wrong frame");

    // Into a loop with K proven: PRE stays until the callbacks past the first wrap, then returns.
    chain.enterLoop (48000);
    const auto entered = chain.withheld;
    chain.run (6);
    require (chain.withheld == entered && chain.exact > 0, "a known K carries into the loop");
    chain.until (lap, [&] { return chain.withheld > entered; }, "the first wrap withholds what POST cannot name");
    chain.settled (14, "the measured cycle returns PRE without another lap");
    const auto looping = chain.withheld;
    chain.run (lap * 3);
    require (chain.withheld == looping, "later wraps keep PRE on");
    chain.clean ("the loop never played a wrong frame");

    // Below zero, inside the loop: PRE's lead bounds K, the next wrap measures the cycle and the
    // same selection returns. Linear playback and a positive K keep working around it.
    chain.setDelay (-512);
    chain.settled (lap * 2, "a negative K proves itself inside the loop without another press");
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "the loop aligns it as well");
    const auto below = chain.withheld;
    chain.run (lap * 2);
    require (chain.withheld == below, "the wraps keep PRE on below zero");
    chain.leaveLoop();
    chain.run (20);
    chain.seek (20200 - 16 * ChunkedPost::chunkFrames);
    chain.settled (40, "linear playback keeps it");
    chain.enterLoop (48000);
    const auto reentered = chain.withheld;
    chain.run (6);
    require (chain.withheld == reentered, "a known negative K carries into the loop");
    chain.setDelay (512);
    chain.settled (lap * 3, "a positive K proves itself inside the loop without another press");
    chain.clean ("re-acquiring never played a wrong frame");
    chain.leaveLoop();
    chain.seek (0);
    chain.settled (40, "linear playback again");
    chain.setDelay (-512);
    chain.settled (40, "BLIND below starts from the early PRE");
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "aligned before BLIND");

    post.finishLiveCompare();
    chain.until (200, [&] { post.serviceLiveBlind(); const auto s = post.liveCompareStatus(); return ! s.active && ! s.finishing; },
                 "END returns to POST");

    require (post.beginLiveBlind() == hypha::live_compare::StartResult::started, "BLIND starts with POST at the hardware buffer");
    chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; }, "BLIND prepares and starts");
    const auto trial = post.liveBlindStatus().trial.epoch;
    const auto gain = post.liveCompareStatus().gain;
    require (gain < 0.75f, "MATCH lowered PRE to POST's level");
    require (post.selectLiveBlind (1), "the first source is selected");
    chain.run (3);
    const bool firstIsPre = post.liveCompareStatus().preAudible;
    require (post.selectLiveBlind (2), "the second source is selected");
    chain.run (3);
    require (post.liveCompareStatus().preAudible != firstIsPre, "the two sources are PRE and POST");
    require (post.selectLiveBlind (firstIsPre ? 1 : 2), "PRE's source is selected");
    chain.settled (10, "BLIND plays PRE");

    chain.contentK = 1536;
    chain.run (400); // two fresh strong estimates, and another service after any hold
    require (post.liveCompareAlignFrames() == 1024 && post.liveCompareStatus().preAudible
             && ! post.liveCompareStatus().contentHeld, "a trial keeps its address until the listener switches");
    auto& revision = LiveTimingFixtureAccess::gainRevision (post);
    const auto coherentRevision = revision.load();
    revision.store (coherentRevision + 1); // writer in progress, no new gain tuple
    for (int i = 0; i < 4; ++i) chain.chunk (false);
    require (! post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 1024,
             "an incomplete gain publication cannot switch the anonymous source");
    revision.store (coherentRevision);
    chain.settled (40, "the same anonymous source returns when its gains are readable");
    post.kirinHostDelayCompensationStateChanged (false);
    chain.run (4);
    require (! post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 1024,
             "compensation OFF withholds output without switching the anonymous source");
    post.kirinHostDelayCompensationStateChanged (true);
    chain.settled (40, "the same anonymous source returns after compensation ON");
    require (post.liveCompareAlignFrames() == 1024, "compensation recovery cannot apply pending alignment in BLIND");
    LiveTimingFixtureAccess::holdContent (post, -2048);
    for (int i = 0; i < 4; ++i) chain.chunk (false);
    require (! post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 1024,
             "a content hold is not an anonymous source switch");
    LiveTimingFixtureAccess::clearContent (post);
    chain.settled (40, "the same anonymous source returns after a content hold");
    require (post.liveCompareAlignFrames() == 1024, "the trial's address survives temporary POST output");
    chain.seek (chain.transport + 96000);
    chain.settled (40, "BLIND returns after a timing interruption");
    require (post.liveCompareAlignFrames() == 1024, "a seek cannot apply the pending address in BLIND");
    require (post.selectLiveBlind (firstIsPre ? 2 : 1), "switch to POST");
    chain.run (4);
    require (post.liveCompareAlignFrames() == 2048, "the listener's switch applies the new value");
    require (post.selectLiveBlind (firstIsPre ? 1 : 2), "switch back to PRE");
    chain.settled (10, "BLIND returns aligned, through the usual fade");

    // A seek in the trial: POST for the unproven blocks, then the same source, gain and assignment.
    const auto interrupted = chain.withheld;
    chain.seek (chain.transport + 96000);
    chain.settled (40, "BLIND returns to the same source after a seek");
    const auto resumed = post.liveBlindStatus();
    require (chain.withheld > interrupted && resumed.stage == BlindStage::active && resumed.trial.epoch == trial,
             "BLIND keeps its trial across the seek");
    const auto after = post.liveCompareStatus();
    require (after.preAudible && after.gain == gain && after.verdict == Verdict::accepted, "the same source at the same gain");
    require (post.revealLiveBlind() && post.liveBlindStatus().trial.usedPostFallback, "REVEAL says POST stood in");
    // The result names what actually happened.
    const auto revealed = [&] (bool timing, bool ceiling)
    {
        auto status = post.liveBlindStatus();
        status.trial.fallbackForTiming = timing;
        status.trial.fallbackForCeiling = ceiling;
        return hypha::live_blind_ui::screenFor (status, true, 1.0f).instruction;
    };
    require (post.liveBlindStatus().trial.fallbackForTiming && ! post.liveBlindStatus().trial.fallbackForCeiling
             && revealed (true, false) == "POST was used briefly when timing was unavailable", "a seek is timing");
    require (revealed (false, true) == "POST played where PRE went over the ceiling", "a level guard is the ceiling");
    require (revealed (true, true) == "POST played for timing and the PRE ceiling", "both name both");
    chain.clean ("BLIND never played a wrong frame");
    post.closeLiveBlind();
    chain.until (200, [&] { post.serviceLiveBlind(); const auto s = post.liveCompareStatus(); return ! s.active && ! s.finishing; },
                 "BLIND closes");

    // Through the real renderer and processor: ceiling fallback must reach REVEAL as a ceiling.
    chain.postScale = 2.0f;
    require (post.beginLiveBlind() == hypha::live_compare::StartResult::started, "a fresh BLIND starts");
    chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; }, "BLIND MATCH completes");
    require (post.liveCompareStatus().gain > 1.0f, "MATCH raises PRE for the ceiling case");
    require (post.selectLiveBlind (1), "select the first anonymous source");
    chain.run (4);
    const auto preSource = post.liveCompareStatus().preAudible ? 1 : 2;
    require (post.selectLiveBlind (3 - preSource), "hear POST before the guard");
    chain.run (4);
    require (post.selectLiveBlind (preSource), "hear PRE before the guard");
    chain.settled (20, "PRE earns its real played receipt");
    chain.loudFrom = chain.engine + ChunkedPost::chunkFrames;
    chain.run (12);
    require (post.liveCompareStatus().observation == hypha::live_compare::RecoveryReason::ceiling,
             "real PRE samples trip the ceiling");
    require (post.revealLiveBlind(), "REVEAL remains available after both real sources played");
    const auto result = post.liveBlindStatus();
    require (result.trial.usedPostFallback && result.trial.fallbackForCeiling && ! result.trial.fallbackForTiming
             && hypha::live_blind_ui::screenFor (result, true, 1.0f).instruction == "POST played where PRE went over the ceiling",
             "ceiling alone never claims unavailable timing");
    chain.clean ("the ceiling fallback played exact POST");
    post.closeLiveBlind();
    chain.until (200, [&] { post.serviceLiveBlind(); const auto s = post.liveCompareStatus(); return ! s.active && ! s.finishing; },
                 "the ceiling trial closes");

    // The choice is saved only when it differs from the default.
    const auto savedAlign = [&]
    {
        juce::MemoryBlock data;
        post.getStateInformation (data);
        return std::string_view (static_cast<const char*> (data.getData()), data.getSize()).find ("live_auto_align")
            != std::string_view::npos;
    };
    require (! savedAlign(), "the default writes nothing");
    post.setLiveCompareAutoAlign (false);
    require (savedAlign(), "off is saved");
    juce::MemoryBlock offState;
    post.getStateInformation (offState);
    post.setLiveCompareAutoAlign (true);
    post.setStateInformation (offState.getData(), static_cast<int> (offState.getSize()));
    require (! post.liveCompareAutoAlign(), "off restores through the real state reader");
    post.setLiveCompareAutoAlign (true);
    juce::MemoryBlock defaultState;
    post.getStateInformation (defaultState);
    post.setLiveCompareAutoAlign (false);
    post.setStateInformation (defaultState.getData(), static_cast<int> (defaultState.getSize()));
    require (post.liveCompareAutoAlign(), "an absent attribute restores the default");
    post.setLiveCompareAutoAlign (false);
    constexpr char legacy[] = R"({"version":"1.1.26","params":{},"fields":{"name":"\"legacy\""}})";
    post.setStateInformation (legacy, static_cast<int> (sizeof (legacy) - 1));
    require (post.liveCompareAutoAlign(), "legacy JSON restores the default after the instance was turned off");

    // A prepare ends the chunk proof on the Audio Thread; only a reported chunk proves it again.
    require (LiveTimingFixtureAccess::chunkProven (post), "the fixture holds a proven chunk");
    hypha::tests::prepareFixtureProcessor (post, 48000, ChunkedPost::chunkFrames);
    LiveTimingFixtureAccess::configureAaxClock (post); // the same certificate: only the generation moved
    chain.chunk();
    require (! LiveTimingFixtureAccess::chunkProven (post), "a prepare ends the proof");
    chain.chunk();
    require (LiveTimingFixtureAccess::chunkProven (post), "the next closed chunk proves it");
    chain.clean ("nothing wrong sounded");
}
}
