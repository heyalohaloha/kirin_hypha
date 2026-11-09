#pragma once
#include "AaxChunkProductFixture.h"
#include <functional>
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"

namespace aax_analysis_product
{
inline std::function<void()> formatInterleave;
inline std::function<void(std::int64_t)> publicationInterleave;

inline void alignmentRechecksPromptly (int channels)
{
    using namespace hypha::live_compare;
    for (int buffer : { 256, 512, 1024 })
    {
        ChunkedPost chain (channels, buffer);
        chain.requireContentPosition = true;
        chain.seek (5000);
        chain.enterLoop (48000);
        auto& post = *chain.post;
        chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select initial LOOP PRE");
        require (post.startLiveCompare() == StartResult::started, "initial LOOP starts LISTEN");
        post.selectLiveComparePre (true);
        chain.until (90, [&] { return post.liveCompareAlignFrames() == 1024; }, "initial LOOP aligns within two seconds");
        chain.clean ("initial LOOP keeps K and the sample address independent");
        chain.leaveLoop();
        chain.run (60);
        auto invalid = post.measureLiveCompareOffset();
        require (offsetCanVote (invalid), "the invalid-offset test starts from real current evidence");
        invalid.lagFrames = INT64_MIN;
        OffsetMonitor::Step invalidStep; invalidStep.settled = true;
        invalidStep.lagFrames = INT64_MIN; invalidStep.sampleRate = 48000;
        require (!post.alignLiveCompare (invalidStep, invalid), "an unrepresentable offset cannot publish a zero correction");
        // Only the host's compensated position moves. The audio delay does not.
        chain.setDelay (512);
        chain.until (90, [&] { return post.liveCompareAlignFrames() == 0 && post.liveCompareStatus().preAudible; },
                     "changed insert compensation replaces the old alignment within two seconds");
        require (post.liveCompareStatus().preSelected && !post.liveCompareStatus().contentHeld,
                 "delay recheck keeps the choice and recovers automatically");
        chain.clean ("rechecking never jumps PRE's read address under audible PRE");
        chain.setDelay (1024); // PRE would need unwritten future audio: keep exact POST
        chain.run (190);
        require (!post.liveCompareStatus().preAudible && post.liveCompareStatus().preSelected,
                 "a confirmed late PRE cannot release the alignment wait");
        const auto late = post.liveCompareStatus();
        require (late.observation == RecoveryReason::preLate && post.liveContentTimingObservation().lagFrames == 512,
                 "confirmed late PRE publishes its measured delay instead of a calibration wait");
        const auto guidance = hypha::live_compare_ui::namedPresentation (late);
        require (guidance.reason == RecoveryReason::preLate && guidance.action == hypha::live_compare_ui::RecoveryAction::none
                 && juce::String (guidance.instruction).contains ("Check chain delay and DAW compensation"),
                 "uncorrectable timing asks to check the chain instead of waiting longer");
        auto ended = late;
        ended.active = ended.preSelected = ended.preWaiting = ended.prolongedWait = false;
        require (*hypha::live_compare_ui::namedPresentation (ended).instruction == 0,
                 "a past late-PRE observation cannot keep a remedy after normal END");
        chain.setDelay (512);
        chain.until (90, [&] { return post.liveCompareStatus().preAudible; }, "a correct mapping resumes the same PRE");
        chain.clean ("the uncorrectable interval never plays the wrong PRE");
    }
}

inline void zeroAlignmentRechecksAmbiguousAudio (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    chain.requireContentPosition = true;
    chain.setDelay (512);
    auto& post = *chain.post;
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select zero-alignment PRE");
    require (post.startLiveCompare() == StartResult::started, "zero-alignment LISTEN");
    post.selectLiveComparePre (true);
    chain.until (90, [&] { return post.liveContentTimingObservation().settled && post.liveCompareStatus().preAudible; },
                 "zero correction is confirmed by content");
    require (post.liveCompareAlignFrames() == 0, "initial content position needs zero correction");
    chain.unrelatedPost = true;
    chain.requireContentPosition = false;
    chain.setDelay (-512);
    for (int i = 0; i < 40; ++i) chain.chunk(); // under one second of proven playback
    require (!post.liveCompareStatus().preAudible && post.liveCompareStatus().preSelected && post.liveCompareAlignPending(),
             "ambiguous content after K changes first waits for fresh evidence");
    chain.run (150);
    require (post.liveCompareStatus().preAudible && !post.liveCompareStatus().prolongedWait && !post.liveCompareAlignPending()
             && post.liveCompareAlignFrames() == 0, "after the bounded wait PRE sounds at the new clock position");
    // A confident late PRE found after the wait is a delay to fix: POST again while it lasts.
    chain.unrelatedPost = false;
    chain.contentK = -1024;
    chain.until (90, [&] { return !post.liveCompareStatus().preAudible
                                  && post.liveCompareStatus().observation == RecoveryReason::preLate; },
                 "a confident late PRE found after the wait returns LISTEN to POST");
    chain.run (100);
    require (!post.liveCompareStatus().preAudible && post.liveCompareAlignPending()
             && post.liveContentTimingObservation().lagFrames == 512,
             "while strong windows keep showing the delay, late PRE never sounds");
    chain.contentK = 512;
    chain.until (90, [&] { return post.liveCompareAlignFrames() == 1024 && post.liveCompareStatus().preAudible; },
                 "fresh strong evidence moves the selected PRE through the usual fades");
    chain.clean ("the recheck sounds only proven clock positions and the confirmed correction");
}

inline void blindRecheckDoesNotRevealAssignment (int channels)
{
    using namespace hypha::live_compare;
    hypha::blind_ui::Screen screens[2];
    for (int assignment = 0; assignment < 2; ++assignment)
    {
        ChunkedPost chain (channels);
        auto& post = *chain.post;
        chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select anonymous timing PRE");
        require (post.beginLiveBlind() == StartResult::started, "anonymous timing begins through BLIND admission");
        chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                     "anonymous timing prepares actual PCM and gains");
        LiveTimingFixtureAccess::assignBlind (post, assignment != 0);
        require (post.selectLiveBlind (1), "select the first anonymous source");
        chain.run (4);
        require (post.liveBlindStatus().trial.played == 1, "only the first source earned an audio receipt");
        chain.unrelatedPost = true;
        chain.setDelay (512);
        chain.until (90, [&] { return post.liveCompareAlignPending(); }, "the new clock mapping starts a content recheck");
        require (post.selectLiveBlind (2), "select the unheard source during the recheck");
        chain.run (4);
        require (post.liveBlindStatus().trial.played == 1 && !post.revealLiveBlind(),
                 "neither assignment earns the second receipt from substitute POST");
        const auto state = post.liveBlindStatus();
        require (state.stage == BlindStage::active && !state.prolongedWait && post.liveCompareAlignPending(),
                 "real changed K and ambiguous content start a short anonymous recheck");
        auto invalidated = state;
        invalidated.stage = BlindStage::invalidated;
        invalidated.reason = RecoveryReason::pairChanged;
        invalidated.observation = RecoveryReason::preLate;
        invalidated.trial.invalidated = true;
        require (hypha::live_compare_ui::blindRecovery (invalidated, true).reason == RecoveryReason::pairChanged,
                 "an old timing observation cannot mask the lost-pair remedy");
        require (!post.liveCompareStatus().preAudible, "rechecking plays only exact POST");
        chain.clean ("anonymous rechecking never reads an unverified address");
        const auto gain = post.liveCompareStatus().gain;
        chain.until (90, [&] { return !post.liveCompareAlignPending() && post.liveCompareAlignFrames() == 0
                                     && post.liveCompareStatus().preAudible == (assignment == 0); },
                     "the bounded wait resumes either anonymous source at its clock position");
        const auto resumed = post.liveBlindStatus();
        screens[assignment] = hypha::live_blind_ui::screenFor (resumed, true, post.liveCompareStatus().postActual);
        require (!resumed.prolongedWait && !screens[assignment].guidanceShown && !screens[assignment].alignOffShown,
                 "both assignments resume without a timing notice");
        chain.unrelatedPost = false;
        require (post.liveBlindStatus().trial.epoch == state.trial.epoch && post.liveBlindStatus().trial.selected == 2
                 && std::abs (post.liveCompareStatus().gain - gain) < 1.0e-9f,
                 "rechecking keeps the same anonymous assignment, selection and approved gain");
        chain.clean ("anonymous recovery uses only the newly confirmed address");
        chain.run (4);
        require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(),
                 "both assignments can reveal after the requested source actually sounded");
    }
    require (screens[0] == screens[1], "every visible BLIND field is identical under the opposite actual assignment");
}

inline void initialAlignmentWaitsForEvidence (int channels)
{
    using namespace hypha::live_compare;
    for (bool quiet : { true })
    for (bool loop : { false, true })
    {
        ChunkedPost chain (channels);
        chain.requireContentPosition = true;
        if (quiet) chain.quietUntil = std::numeric_limits<std::int64_t>::max();
        chain.tonal = !quiet;
        if (loop) { chain.seek (5000); chain.enterLoop (48000); }
        auto& post = *chain.post;
        chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select ambiguous initial PRE");
        require (post.startLiveCompare() == StartResult::started, "ambiguous initial LISTEN starts");
        post.selectLiveComparePre (true);
        chain.requireContentPosition = false;
        chain.run (190);
        const auto waiting = post.liveCompareStatus();
        require (waiting.preSelected && waiting.preAudible && !post.liveCompareAlignPending()
                 && post.liveCompareAlignFrames() == 0, "initial silence plays PRE at its clock position after the bounded wait");
        require (waiting.observation != RecoveryReason::contentAmbiguous && !waiting.prolongedWait,
                 "a validated ambiguous window leaves no wait behind");
        require (*hypha::live_compare_ui::namedPresentation (waiting).instruction == 0,
                 "unconfirmed content shows no notice once PRE plays");
        chain.clean ("initial ambiguous samples use only the proven clock position");
        chain.quietUntil = chain.unwritten(); // PRE sounds at K: its ring already holds the next chunk
        chain.unrelatedPost = false;
        chain.until (90, [&] { return post.liveCompareAlignFrames() == 1024 && post.liveCompareStatus().preAudible; },
                     "a usable section aligns the same PRE without another selection");
        require (post.liveCompareStatus().preSelected, "automatic initial recovery keeps the choice");
        chain.clean ("every sample through the first confirmation uses the correct source position");
    }
}

inline void disabledAlignmentStillMonitorsContent (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    auto& post = *chain.post;
    post.setLiveCompareAutoAlign (false);
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select disabled-alignment PRE");
    require (post.startLiveCompare() == StartResult::started, "disabled-alignment LISTEN starts");
    post.selectLiveComparePre (true);
    const auto observed = [&] (std::int64_t lag) {
        const auto step = post.liveContentTimingObservation();
        const auto estimate = post.measureLiveCompareOffset();
        return step.settled && step.lagFrames == lag && offsetCanVote (estimate) && estimate.clockK == chain.k;
    };
    chain.until (300, [&] { return observed (-1024); }, "OFF with nonzero K still measures the initial difference");
    require (post.liveCompareAlignFrames() == 0 && post.liveCompareStatus().preAudible,
             "OFF leaves the read position at K while monitoring");
    chain.setDelay (256);
    chain.until (300, [&] { return observed (-256); }, "OFF follows changed K and displays its current difference");
    require (post.liveCompareAlignFrames() == 0 && !post.liveCompareAutoAlign(), "monitoring cannot turn correction on");
    chain.contentK = 1536;
    chain.until (300, [&] { return post.liveCompareStatus().contentHeld; },
                 "OFF retains strong content-change detection after K changes");
    require (post.liveCompareStatus().preSelected, "publishing a content hold preserves the explicit choice");
    chain.chunk (false); // the first real callback after the non-RT monitor publishes the hold
    require (!post.liveCompareStatus().preAudible && post.liveCompareStatus().preSelected,
             "the first callback after a confirmed content jump uses POST while preserving the choice");
    // INV-LC10: only confident windows keep a hold. While strong windows keep showing the jump,
    // shifted PRE never sounds; a limiter-like chain gives none, so PRE returns a second later.
    chain.run (100);
    require (post.liveCompareStatus().contentHeld && !post.liveCompareStatus().preAudible,
             "a confident jump that persists keeps POST past the bounded wait");
    chain.chordFrom = chain.unwritten();
    chain.limited = true;
    chain.until (200, [&] { return !post.liveCompareStatus().contentHeld && post.liveCompareStatus().preAudible; },
                 "windows that confirm nothing release the hold after one second");
    require (post.liveCompareAlignFrames() == 0 && post.liveCompareStatus().preSelected,
             "released PRE plays at its clock position with the same selection");
    chain.chordUntil = chain.unwritten();
    chain.limited = false;
    chain.until (300, [&] { return post.liveCompareStatus().contentHeld; }, "the same confident jump holds again");
    chain.contentK = 512;
    chain.until (300, [&] { return !post.liveCompareStatus().contentHeld && post.liveCompareStatus().preAudible; },
                 "OFF monitoring returns automatically when the content baseline returns");
    chain.clean ("OFF monitoring leaves each sample on its selected verified clock path");
}

inline void alignmentRejectsChangingFormat (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    auto& post = *chain.post;
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select format fixture PRE");
    require (post.startLiveCompare() == StartResult::started, "format fixture LISTEN");
    post.selectLiveComparePre (true);
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "format fixture settles");
    require (std::abs (post.liveCompareAlignMs() - 1024.0 * 1000.0 / 48000.0) < 1.0e-9,
             "display converts aligned frames at the prepared rate");
    post.prepareToPlay (48000, ChunkedPost::chunkFrames);
    require (post.liveCompareAlignFrames() == 1024, "same-format prepare keeps the display");
    const auto before = post.measureLiveCompareOffset();
    require (offsetCanVote (before), "old history is measurable before prepare");
    OffsetMonitor monitor;
    monitor.observe (before, false);
    const auto step = monitor.observe (before, false);
    bool visited = false, measured = true, published = true;
    double displayed = -1.0;
    formatInterleave = [&] {
        visited = true;
        require (LiveTimingFixtureAccess::alignmentHistoryValid (post), "interleave is before old history retires");
        std::thread service ([&] {
            displayed = post.liveCompareAlignMs();
            measured = post.measureLiveCompareOffset().determined;
            published = post.alignLiveCompare (step, before);
        });
        service.join();
    };
    afterAlignFormatChangeForTest = [] { formatInterleave(); };
    post.prepareToPlay (44100, ChunkedPost::chunkFrames);
    afterAlignFormatChangeForTest = nullptr;
    formatInterleave = {};
    require (displayed == 0.0, "display cannot read frames or rate during a format write");
    require (post.liveCompareAlignMs() == 0.0 && post.liveCompareAlignFrames() == 0,
             "completed format cannot pair old frames with its new rate before another callback");
    require (visited && ! measured && ! published, "prepare's new epoch cannot measure or publish old PCM");
    post.prepareToPlay (48000, ChunkedPost::chunkFrames);
    LiveTimingFixtureAccess::configureAaxClock (post);
    LiveTimingFixtureAccess::enableFixtureWrites (post);
    chain.chunk (false);
    LiveTimingFixtureAccess::serviceTiming (post);
    require (post.startLiveCompare() == StartResult::started, "LISTEN restarts in the completed format");
    post.selectLiveComparePre (true);
    chain.postScale = 0.0f;
    chain.run (220);
    require (post.liveCompareStatus().preAudible && post.liveCompareStatus().preSelected && post.liveCompareAlignFrames() == 0,
             "undetermined new content plays PRE only at the new format's clock position");
    chain.clean ("format interleave never emits a stale address");
}

inline void alignmentBelongsToPreOwner (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    auto& post = *chain.post;
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select owner fixture PRE");
    require (post.startLiveCompare() == StartResult::started, "owner fixture LISTEN");
    post.selectLiveComparePre (true);
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "old PRE settles");
    const auto old = post.measureLiveCompareOffset();
    const auto identity = chain.pre->instanceId();
    chain.replacePre();
    require (chain.pre->instanceId() == identity, "only PRE's owner changes, its instance ID remains");
    chain.run (4);
    require (post.startLiveCompare() == StartResult::started, "same paired PRE can start with a new owner");
    post.selectLiveComparePre (true);
    const auto* mapping = LiveTimingFixtureAccess::publishedRing (post);
    require (mapping != nullptr && mapping->ring() != nullptr, "new PRE ring is open");
    const auto& timing = mapping->ring()->header.timing;
    require (timing.ownerA.load() != old.alignContext.ownerA || timing.ownerB.load() != old.alignContext.ownerB,
             "same instance ID has a different ring owner");
    LiveTimingFixtureAccess::publishMeasuredAlign (post, old); // a delayed old publication
    chain.postScale = 0.0f;
    chain.run (220);
    require (post.liveCompareStatus().preAudible && post.liveCompareStatus().preSelected && post.liveCompareAlignFrames() == 0,
             "new PRE with ambiguous content plays at its clock position, never the old owner's correction");
    chain.postScale = 0.5f;
    chain.contentK = 1536;
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 2048; }, "new owner measures its own correction");
    chain.clean ("PRE replacement never reads an old correction");
}

// Anti-phase and single-channel material must reach the default-ON path through actual PCM,
// including BLIND's played receipts. The fixture independently checks every output sample.
inline void stereoAlignmentPreservesChannelEvidence (int channels)
{
    if (channels != 2) return;
    using namespace hypha::live_compare;
    for (int mode : { 0, 1, 2, 3, 4 })
    {
        ChunkedPost chain (channels);
        chain.requireContentPosition = true;
        chain.oppositePolarity = mode == 0;
        chain.silentChannel = mode == 1 ? 0 : mode == 2 ? 1 : -1;
        if (mode == 3) chain.rightContentShift = 512;
        chain.swappedChannels = mode == 4;
        chain.seek (5000); chain.enterLoop (48000);
        auto& post = *chain.post;
        require (post.liveCompareAutoAlign(), "stereo evidence is tested with the default alignment ON");
        chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select stereo evidence PRE");
        require (post.startLiveCompare() == StartResult::started, "stereo evidence starts LISTEN");
        post.selectLiveComparePre (true);
        if (mode == 3)
        {
            chain.requireContentPosition = false;
            chain.run (190);
            require (post.liveCompareStatus().preAudible && post.liveCompareStatus().preSelected
                     && !post.liveCompareAlignPending() && post.liveCompareAlignFrames() == 0,
                     "disagreeing channel positions move nothing; PRE plays at its clock position");
            chain.rightContentShift = 0;
        }
        chain.until (90, [&] { return post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 1024; },
                     "inverse polarity or either single channel aligns inside the initial loop");
        chain.clean ("every stereo sample uses the verified position");
        if (mode != 0) continue;
        post.finishLiveCompare();
        chain.until (30, [&] { return !post.liveCompareStatus().active && !post.liveCompareStatus().finishing; },
                     "LISTEN ends before a fresh anti-phase BLIND");
        require (post.beginLiveBlind() == StartResult::started, "anti-phase BLIND starts");
        chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                     "anti-phase BLIND prepares from actual stereo PCM");
        for (int source : { 1, 2 })
        {
            require (post.selectLiveBlind (source), "select each anti-phase anonymous source");
            chain.run (12);
        }
        require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(),
                 "anti-phase BLIND earns both real played receipts and can REVEAL");
        chain.clean ("anti-phase BLIND uses the verified sample position");
    }
}

inline std::function<void()> offsetWindowInterleave;
inline void copiedOffsetWindowVotesOnlyOnce (int channels)
{
    using namespace hypha::live_compare;
    for (bool recheck : { false, true })
    {
        ChunkedPost chain (channels);
        chain.requireContentPosition = true;
        auto& post = *chain.post;
        chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select copied-window PRE");
        require (post.startLiveCompare() == StartResult::started, "copied-window LISTEN starts");
        post.selectLiveComparePre (true);
        if (recheck)
        {
            chain.until (90, [&] { return post.liveCompareStatus().preAudible; }, "initial position is confirmed before recheck");
            chain.setDelay (512);
        }
        // Accumulate a complete current window without servicing its monitor. The hook inserts
        // the next real callback between service entry and the estimator's actual snapshot.
        for (int i = 0; i < 60; ++i) chain.chunk (false);
        const auto before = post.measureLiveCompareOffset();
        require (offsetCanVote (before), "the contention test has strong current PCM");
        bool visited = false;
        offsetWindowInterleave = [&] { visited = true; chain.chunk (false); };
        beforeOffsetWindowForTest = [] { offsetWindowInterleave(); };
        LiveTimingFixtureAccess::serviceContentTiming (post);
        beforeOffsetWindowForTest = nullptr;
        offsetWindowInterleave = {};
        const auto copied = post.measureLiveCompareOffset();
        require (visited && offsetCanVote (copied) && copied.window.end > before.window.end,
                 "a real callback advanced the copied window within the acquisition interval");
        require (!post.liveContentTimingObservation().settled, "one copied window supplies only the first vote");
        chain.wallClock.advance (std::chrono::milliseconds (120));
        LiveTimingFixtureAccess::serviceContentTiming (post); // no additional PCM
        require (!post.liveContentTimingObservation().settled && post.liveCompareAlignPending(),
                 "the same copied window cannot become a second vote after callbacks cease");
        require (post.liveCompareStatus().preSelected, "duplicate evidence never clears the selected source");
        // Resume with a normal callback interval; wall time elapsed during the pause is genuine.
        chain.until (90, [&] { return post.liveCompareStatus().preAudible && !post.liveCompareAlignPending(); },
                     "fresh PCM automatically completes initial alignment or recheck");
        chain.clean ("window contention never emits an unverified PRE address");
    }
}

// Advance K between the service's generation check and the estimator's own snapshot. Both
// generations have the same lag, so an old candidate would otherwise confirm after one window.
inline void newMappingStartsWithOneVote (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    chain.requireContentPosition = true;
    auto& post = *chain.post;
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select generation PRE");
    require (post.startLiveCompare() == StartResult::started, "generation LISTEN starts");
    post.selectLiveComparePre (true);
    for (int i = 0; i < 60; ++i) chain.chunk (false);
    LiveTimingFixtureAccess::serviceContentTiming (post);
    require (!post.liveContentTimingObservation().settled, "old generation has exactly one vote");
    chain.wallClock.advance (std::chrono::milliseconds (120));
    bool visited = false;
    offsetWindowInterleave = [&] {
        visited = true;
        chain.setDelay (0);
        chain.contentK += 512;
        for (int i = 0; i < 60; ++i) chain.chunk (false);
    };
    beforeContentEstimateForTest = [] { offsetWindowInterleave(); };
    LiveTimingFixtureAccess::serviceContentTiming (post);
    beforeContentEstimateForTest = nullptr;
    offsetWindowInterleave = {};
    require (visited && offsetCanVote (post.measureLiveCompareOffset()), "new generation supplies strong actual PCM");
    require (!post.liveContentTimingObservation().settled && post.liveCompareAlignPending(),
             "new generation cannot combine its first vote with the previous generation");
    chain.until (90, [&] { return post.liveCompareStatus().preAudible && !post.liveCompareAlignPending(); },
                 "a second fresh window resumes the retained PRE selection");
    chain.clean ("generation change never emits an unconfirmed address");
}

}
