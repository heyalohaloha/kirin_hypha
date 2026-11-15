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
}
}
