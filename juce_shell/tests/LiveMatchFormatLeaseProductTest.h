#pragma once
#include "LiveTimingFixtureAccess.h"
#include <functional>
#include <thread>

namespace hypha::tests::match_format_fixture
{
#if defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
inline std::function<void()> atPreparedFormatBoundary;
inline std::function<void()> atCountedMatchRead;

inline void verifyReaderFirst (KirinHyphaProcessorBase& post, const juce::String& preId,
                               live_compare::SharedRingMapping& observer, int blockSize,
                               void (*require) (bool, const char*))
{
    using namespace live_compare;
    // Re-arm the same real PRE after the incompatible prepare. No audio callback is running.
    post.prepareToPlay (observer.rate(), blockSize);
    LiveTimingFixtureAccess::enableFixtureWrites (post);
    require (LiveTimingFixtureAccess::configureStudioProClock (post),
             "reader-first control restores the qualified VST3 clock");
    if (post.liveComparePreInstance() != preId)
        require (post.setPairCandidate (preId, {}), "reader-first control reselects the original PRE");
    require (post.liveComparePreInstance() == preId,
             "reader-first control reopens the original PRE in its native format");
    auto& demand = observer.ring()->header.demand; // separate clock-only view; not the retiring POST mapping
    const auto baseline = demand.load();
    for (const bool whole : { false, true })
    {
        post.setWholeMatchPreferred (whole);
        require (post.startLiveCompare() == StartResult::started
                 && LiveTimingFixtureAccess::wholeSpanEnabledForTest (post) == whole,
                 "reader-first control starts the requested real LISTEN span");
        const auto leased = demand.load();
        require (leased == baseline + 1, "LISTEN POST owns exactly one PCM demand");
        bool retired = false;
        atCountedMatchRead = [&] {
            require (! retired && LiveTimingFixtureAccess::timingRingPublished (post)
                     && demand.load() == leased,
                     "the read hook runs once after acquiring the current mapping");
            // Real format teardown withdraws publication and tries collect, still within the
            // measurement's counted read. Regressions fail here before any unmapped read.
            LiveTimingFixtureAccess::retirePreparedFormatRing (post);
            require (! LiveTimingFixtureAccess::timingRingPublished (post) && demand.load() == leased,
                     "retirement withdraws the POST mapping but cannot destroy it while MATCH still reads");
            retired = true;
        };
        const juce::ScopedValueSetter<decltype (duringLiveMatchReadLeaseForTest)> scopedHook (
            duringLiveMatchReadLeaseForTest, +[] { atCountedMatchRead(); });
        const auto result = post.measureLiveCompare (whole);
        atCountedMatchRead = {};
        require (retired && ! result.ok() && demand.load() == leased,
                 "the retired mapping survived the last read and the retired session yields no proof");
        LiveTimingFixtureAccess::retirePreparedFormatRing (post); // now the same slot can collect
        require (! LiveTimingFixtureAccess::timingRingPublished (post) && demand.load() == baseline,
                 "collect after the MATCH reader exits releases exactly its POST PCM demand");
    }
}
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
    const auto preId = post.liveComparePreInstance();
    const auto* initial = LiveTimingFixtureAccess::publishedRing (post);
    SharedRingMapping observer;
    require (initial != nullptr && observer.open (initial->key(), initial->rate(), false),
             "test observes PRE demand through an independent non-demand mapping");
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
    verifyReaderFirst (post, preId, observer, blockSize, require);
#else
    (void) post; (void) recent; (void) whole; (void) blockSize;
    require (false, "Y1 requires the production format interleave test hook");
#endif
}
}
