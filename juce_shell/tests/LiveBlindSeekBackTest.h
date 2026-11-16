#pragma once
#include "CandidateProductHost.h"

namespace hypha::tests
{
// BLIND started on silence past the song, then the DAW goes back to the song and plays. MATCH
// measures the new audio as soon as it comes (not only once the old end is passed again), BLIND
// goes on by itself, and the wait says its reason all the time it waits.
template<class Require> void verifyLiveBlindSeekBack (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    host.inputScale = 0.0f; host.render (100, false, false);
    require (post.beginLiveBlind() == StartResult::started, "BLIND starts on silence past the song");
    host.render (1000, false, false);
    require (post.liveBlindStatus().stage == BlindStage::preparing && post.liveBlindStatus().prolongedWait,
             "silence keeps BLIND preparing and says why");
    host.cursor -= 480000; // the DAW goes back ten seconds, to the song: its clock moves back too
    host.inputScale = 1.0f;
    int goesOn = -1, silentReason = 0;
    for (int n = 0; n < 1400 && goesOn < 0; ++n)
    {
        host.render (1, false, false);
        const auto status = post.liveBlindStatus();
        if (status.stage != BlindStage::preparing) goesOn = n;
        else if (n > 60 && ! status.prolongedWait) ++silentReason;
    }
    const auto stage = post.liveBlindStatus().stage;
    require (goesOn >= 0 && goesOn < 900 && stage != BlindStage::failed && stage != BlindStage::invalidated,
             "after the seek back BLIND goes on once the song's audio arrives");
    require (silentReason == 0, "the wait keeps its reason all the time it waits");
}

// BLIND's own MATCH waits for 30 s of playback at most. Stopped time is the user's and is not counted.
// Past it the preparation ends with its reason and POST is normal again; nothing else waits on it.
template<class Require> void verifyLiveBlindPreparationDeadline (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    host.inputScale = 0.0f; host.render (100, false, false);
    require (post.beginLiveBlind() == StartResult::started, "BLIND starts on silence");
    for (auto& clock : host.clocks) clock.playing = false;
    host.render (2000, false, false);
    for (auto& clock : host.clocks) clock.playing = true;
    host.render (2600, false, false);
    require (post.liveBlindStatus().stage == BlindStage::preparing, "stopped time does not count toward the wait");
    host.render (300, false, false);
    const auto status = post.liveBlindStatus();
    require (status.stage == BlindStage::failed && status.waiting == MatchFailure::notEnoughSignal
                 && ! post.liveCompareStatus().active,
             "thirty seconds of playback end the preparation with its reason and POST is normal");
    const auto screen = live_blind_ui::screenFor (status, true, post.liveCompareStatus().postActual);
    require (! screen.guidanceShown && screen.instruction == "No MATCH: not enough signal",
             "the screen retains the signal failure instead of waiting for more playback");
}

// The DAW stops during the preparation and calls nothing while it is stopped (its last callback said
// playing): that time is not counted. A message thread that stalls while the DAW plays loses none of it.
template<class Require> void verifyLiveBlindPreparationPlayedTime (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    host.inputScale = 0.0f; host.render (100, false, false);
    require (post.beginLiveBlind() == StartResult::started, "BLIND starts on silence");
    host.render (100, false, false);
    for (int tick = 0; tick < 800; ++tick) // 40 s stopped: no callback, while the message thread still runs
    { wall.advance (std::chrono::milliseconds (50)); post.serviceLiveBlind(); }
    require (post.liveBlindStatus().stage == BlindStage::preparing, "stopped time without callbacks is not counted");
    host.render (2200, false, false);
    require (post.liveBlindStatus().stage == BlindStage::preparing, "about 25 s of playback still prepare");
    host.serviceBlindAfterAudio = false; host.render (600, false, false); host.serviceBlindAfterAudio = true;
    post.serviceLiveBlind();
    const auto status = post.liveBlindStatus();
    require (status.stage == BlindStage::failed && status.waiting == MatchFailure::notEnoughSignal,
             "the playback a stalled message thread did not see still counts toward the 30 s");
}

// MATCH is applied, then the clock goes missing before the audio takes it: the preparation stays waiting
// for the audio (settling). The same 30 s of playback end it with its reason, and POST is normal.
template<class Require> void verifyLiveBlindSettlingDeadline (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    require (post.beginLiveBlind() == StartResult::started, "BLIND starts on the song");
    int applied = -1;
    for (int n = 0; n < 1400 && applied < 0; ++n)
    {
        host.render (1, false, false);
        if (post.liveBlindStatus().stage == BlindStage::settling) applied = n + 1;
    }
    require (applied > 0, "the preparation's MATCH is applied");
    for (auto& clock : host.clocks) clock.available = clock.auxiliaryAvailable = false; // from the next callback
    host.render (2700 - applied, false, false);
    const auto waiting = post.liveBlindStatus().stage;
    require (waiting != BlindStage::active && waiting != BlindStage::failed, "without a clock the sources stay unready");
    host.render (250, false, false);
    const auto status = post.liveBlindStatus();
    require (status.stage == BlindStage::failed && status.waiting == MatchFailure::notProven
                 && ! post.liveCompareStatus().active && post.liveCompareStatus().postTarget == 1.0f,
             "30 s of playback end a preparation waiting for its applied MATCH, with its reason");
}

// LISTEN's MATCH lowered POST, then BLIND starts from that LISTEN and its timing is never confirmed. Past
// the 30 s the preparation ends with its reason and POST rises to its normal level by itself (no END).
template<class Require> void verifyLiveBlindDeadlineReturnsPost (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    require (post.startLiveCompare() == StartResult::started, "LISTEN starts before its MATCH lowers POST");
    host.render (100, false, false);
    MatchResult level; level.failure = MatchFailure::none; level.measuredDb = 8.0;
    level.prePeakDbtp = -1.0; level.postPeakDbtp = -4.0; level.ceilingDbtp = -1.0;
    require (post.applyLiveCompareMatch (planMatch (level, 0.0), MatchChoice::lowerPost), "the approved MATCH lowers POST");
    host.render (60, false, false);
    require (post.liveCompareStatus().postActual < 0.5f, "POST plays lowered in LISTEN");
    require (post.beginLiveBlind() == StartResult::started, "BLIND starts from the lowered LISTEN");
    for (auto& clock : host.clocks) clock.available = clock.auxiliaryAvailable = false;
    host.render (2700, false, false);
    require (post.liveBlindStatus().stage == BlindStage::preparing && post.liveCompareStatus().postActual < 0.5f,
             "the lowered level holds while BLIND prepares");
    host.render (250, false, false);
    const auto status = post.liveBlindStatus();
    require (status.stage == BlindStage::failed && status.waiting == MatchFailure::notProven
                 && ! post.liveCompareStatus().active, "30 s of playback end the preparation with its reason");
    const auto failedScreen = live_blind_ui::screenFor (status, true, post.liveCompareStatus().postActual);
    require (! failedScreen.guidanceShown && ! failedScreen.alignOffShown
                 && failedScreen.instruction == "No MATCH: timing not confirmed",
             "the actual screen puts the final reason ahead of recovery guidance");
    host.render (60, false, false);
    const auto after = post.liveCompareStatus();
    require (after.postTarget == 1.0f && after.postActual == 1.0f && ! after.finishing
                 && post.liveBlindStatus().stage == BlindStage::failed && post.liveBlindStatus().waiting == MatchFailure::notProven,
             "POST rises to normal by itself while the reason stays on the screen");
    host.render (400, false, false); // more than three seconds after unity, with callbacks still going
    const auto later = post.liveBlindStatus();
    const auto laterScreen = live_blind_ui::screenFor (later, true, post.liveCompareStatus().postActual);
    require (later.stage == BlindStage::failed && later.waiting == MatchFailure::notProven
                 && ! laterScreen.guidanceShown && ! laterScreen.alignOffShown
                 && laterScreen.instruction == "No MATCH: timing not confirmed",
             "the actual screen keeps the final reason after the return and the old wait notice ages");
    auto stale = later; stale.trial.invalidated = true; stale.prolongedWait = stale.contentHeld = true;
    stale.observation = RecoveryReason::contentAmbiguous;
    const auto staleScreen = live_blind_ui::screenFor (stale, true, 1.0f);
    require (! staleScreen.guidanceShown && ! staleScreen.alignOffShown
                 && staleScreen.instruction == "No MATCH: timing not confirmed",
             "a failed preparation never revives stale recovery controls or hides its reason");
}
}
