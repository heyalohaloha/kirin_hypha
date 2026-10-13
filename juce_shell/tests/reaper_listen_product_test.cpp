#include "ContentAnalysisProductFixture.h"
#include "LiveBlindStatusProductChecks.h"
using namespace content_analysis_product;
using namespace hypha::live_compare;
int main (int argc, char** argv)
try
{
    const auto started = hypha::tests::AnalysisTestClock::now();
    require (argc == 2, "explicit mono/stereo layout");
    ReferenceValidationStorageSandbox sandbox;
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    juce::ScopedJuceInitialiser_GUI initialise;
    // Match this CTest item's 90-second watchdog, including setup and all waits.
    Fixture fixture (juce::String (argv[1]) == "mono" ? 1 : 2, "missing", false, true,
                     hypha::tests::analysisPhaseDeadline (started, 90));
    auto& post = *fixture.post;
    fixture.until ([&] { return post.setPairCandidate (fixture.pre->instanceId(), {}); }, "select PRE");
    require (post.startLiveCompare() == StartResult::started, "REAPER LISTEN starts");
    post.selectLiveComparePre (true); fixture.auditionAllowed = true;
    fixture.until ([&] { return post.liveCompareStatus().preAudible; }, "compensated project calibrates PRE");
    fixture.postClock.positionAvailable = false;
    fixture.auditionAllowed = false; // the entire missing block must be bit-identical POST
    fixture.block();
    require (! post.liveCompareStatus().preAudible, "one missing project block removes named PRE immediately");
    fixture.postClock.positionAvailable = true;
    fixture.auditionAllowed = true;
    require (post.liveCompareStatus().preSelected, "missing position retains explicit PRE selection");
    fixture.until ([&] { return post.liveCompareStatus().preAudible; }, "named PRE automatically reacquires proof");
    MatchResult measured;
    fixture.until ([&] { measured = post.measureLiveCompare(); return measured.ok(); }, "REAPER MATCH window");
    require (std::abs (measured.measuredDb) < 0.01, "same physical content MATCH is zero dB");
    const auto plan = planMatch (measured, 0.0);
    require (post.applyLiveCompareMatch (plan, MatchChoice::basis), "explicit MATCH applies");
    fixture.until ([&] { return post.liveCompareStatus().matched; }, "MATCH current receipt");
    require (post.beginLiveBlind() == StartResult::started, "MATCH to BLIND");
    fixture.until ([&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; }, "BLIND active");
    require (post.selectLiveBlind (1), "anonymous source 1");
    fixture.until ([&] { post.serviceLiveBlind(); return LiveTimingFixtureAccess::audible (post) == 1; }, "source 1 receipt");
    require (post.selectLiveBlind (2), "anonymous source 2");
    fixture.until ([&] { post.serviceLiveBlind(); return LiveTimingFixtureAccess::audible (post) == 2; }, "source 2 receipt");
    const auto command = LiveTimingFixtureAccess::command (post);
    require (post.selectLiveBlind (command.firstPre() ? 1 : 2), "choose anonymous PRE before proof loss");
    fixture.until ([&] { return post.liveCompareStatus().preAudible; }, "anonymous PRE audible");
    const auto trial = post.liveBlindStatus().trial;
    const auto gain = post.liveCompareStatus().gain;
    fixture.postClock.positionAvailable = false;
    fixture.auditionAllowed = false;
    const auto beforeMissing = fixture.callbacks;
    const auto beforeNanos = fixture.wallClock.nanos();
    fixture.block();
    require (fixture.callbacks == beforeMissing + 1
        && fixture.wallClock.nanos() - beforeNanos >= 21'333'333
        && fixture.wallClock.nanos() - beforeNanos <= 21'333'334,
        "exactly one ordinary 2048-frame callback at 96 kHz, without a wall gap");
    require (post.liveCompareStatus().observation == RecoveryReason::projectClockMissing,
             "the callback records the missing project witness as a recoverable observation");
    for (int i = 0; i < 2; ++i)
    {
        const auto waiting = post.liveBlindStatus();
        require (waiting.stage == BlindStage::active && waiting.trial.epoch == trial.epoch
            && waiting.trial.selected == trial.selected && waiting.trial.firstPre == trial.firstPre
            && waiting.trial.usedPostFallback && !post.liveCompareStatus().preAudible,
            "missing proof outputs POST while preserving the anonymous assignment and fallback fact");
        post.serviceLiveBlind();
    }
    fixture.postClock.positionAvailable = true;
    fixture.auditionAllowed = true;
    fixture.until ([&] { post.serviceLiveBlind(); return post.liveCompareStatus().preAudible; },
                   "fresh proof automatically restores anonymous PRE");
    const auto recoveredGain = post.liveCompareStatus().gain;
    require (post.liveBlindStatus().trial.epoch == trial.epoch
        && std::memcmp (&recoveredGain, &gain, sizeof (gain)) == 0,
             "recovered BLIND keeps its epoch and approved gain");
    post.finishLiveCompare();
    fixture.until ([&] { return !post.liveCompareStatus().finishing; }, "END completes");
    std::cout << "REAPER LISTEN/MATCH/BLIND product " << argv[1] << " PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
