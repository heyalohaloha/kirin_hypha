#pragma once
#include "LiveTimingFixtureAccess.h"
#include <functional>
#include <thread>

namespace hypha::tests::match_format_fixture
{
#if defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
inline std::function<void()> atPreparedFormatBoundary;
#endif

// Audio is quiescent per the host contract. Only the message-side MATCH paths race a
// real incompatible prepare; no callback drives the processor with an invalid format.
inline void verify (KirinHyphaProcessorBase& post, const live_compare::MatchResult& recent,
                    const live_compare::MatchResult& whole, int blockSize, void (*require) (bool, const char*))
{
    using namespace live_compare;
#if defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
    require (recent.ok() && ! recent.wholeOwnerBound && whole.ok() && whole.wholeOwnerBound,
             "both real span paths are usable before the format writer enters");
    const auto recentPlan = planMatch (recent, 0.0), wholePlan = planMatch (whole, 0.0);
    const auto choice = [] (const MatchPlan& plan) { return plan.needsApproval ? MatchChoice::lowerPost : MatchChoice::basis; };
    bool visited = false, hadOldRing = false, hadOpenListen = false;
    bool recentMeasured = true, wholeMeasured = true;
    MatchApplication appliedRecent, appliedWhole;
    atPreparedFormatBoundary = [&] {
        visited = true;
        hadOldRing = LiveTimingFixtureAccess::timingRingPublished (post);
        hadOpenListen = post.liveCompareStatus().active;
        // The format writer already owns handleLock but has not retired the old mapping.
        // Try-lock in MATCH must be nonblocking; waiting here would deadlock and fail CTest.
        std::thread menu ([&] {
            recentMeasured = post.measureLiveCompare (false).ok();
            wholeMeasured = post.measureLiveCompare (true).ok();
            appliedRecent = post.applyLiveCompareMatch (recentPlan, choice (recentPlan));
            appliedWhole = post.applyLiveCompareMatch (wholePlan, choice (wholePlan));
        });
        menu.join();
    };
    afterAlignFormatChangeForTest = [] { atPreparedFormatBoundary(); };
    post.prepareToPlay (44100, blockSize); // Actual processor retirement and rebuild (not directly seeded).
    afterAlignFormatChangeForTest = nullptr;
    atPreparedFormatBoundary = {};
    require (visited && hadOldRing && hadOpenListen,
             "MATCH runs inside incompatible prepare before old ring retirement");
    require (! recentMeasured && ! wholeMeasured
             && appliedRecent.failure == MatchFailure::stale && appliedWhole.failure == MatchFailure::stale,
             "no LAST 4 s or ALL PLAYED measure/apply escapes an in-flight incompatible prepare");
    require (! LiveTimingFixtureAccess::timingRingPublished (post) && ! post.liveCompareStatus().active
             && ! post.measureLiveCompare (false).ok() && ! post.measureLiveCompare (true).ok()
             && ! post.applyLiveCompareMatch (recentPlan, choice (recentPlan))
             && ! post.applyLiveCompareMatch (wholePlan, choice (wholePlan)),
             "the rebuilt processor cannot read or apply either pre-prepare proof");
#else
    (void) post; (void) recent; (void) whole; (void) blockSize;
    require (false, "Y1 requires the production format interleave test hook");
#endif
}
}
