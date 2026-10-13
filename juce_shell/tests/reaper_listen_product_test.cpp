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
    post.selectLiveComparePre (true); fixture.auditionAllowed = true;
    fixture.until ([&] { return post.liveCompareStatus().preAudible; }, "explicit named PRE reacquires proof");
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
    fixture.postClock.positionAvailable = false;
    fixture.auditionAllowed = false;
    const auto beforeMissing = fixture.callbacks;
    const auto beforeNanos = fixture.wallClock.nanos();
    fixture.block();
    require (fixture.callbacks == beforeMissing + 1
        && fixture.wallClock.nanos() - beforeNanos >= 21'333'333
        && fixture.wallClock.nanos() - beforeNanos <= 21'333'334,
        "exactly one ordinary 2048-frame callback at 96 kHz, without a wall gap");
    require (LiveTimingFixtureAccess::blindTrial (post).reason == RecoveryReason::projectClockMissing,
             "the callback records the missing project witness as its first fault");
    hypha::tests::requireInterruptedBlind (post, RecoveryReason::projectClockMissing);
    post.serviceLiveBlind();
    hypha::tests::requireInterruptedBlind (post, RecoveryReason::projectClockMissing);
    fixture.postClock.positionAvailable = true;
    for (int i = 0; i < 40; ++i)
    {
        fixture.block(); post.serviceLiveBlind();
        hypha::tests::requireInterruptedBlind (post, RecoveryReason::projectClockMissing);
    }
    post.finishLiveCompare();
    fixture.until ([&] { return !post.liveCompareStatus().finishing; }, "END completes");
    std::cout << "REAPER LISTEN/MATCH/BLIND product " << argv[1] << " PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
