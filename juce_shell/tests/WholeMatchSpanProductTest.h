#pragma once
#include "CandidateProductHost.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace hypha::tests
{
template<class Require> void renderWholeMatchProductBlocks (CandidateHost<Require>& host, int count)
{
    for (int n = 0; n < count; ++n)
    {
        host.render (1, false, false);
        // The real non-RT service normally drains the bounded transfer buffer on a timer.
        // Drive it explicitly without running timers that would also service BLIND.
        if ((n + 1) % 16 == 0)
            host.post().serviceLiveMatchSpan();
    }
    host.post().serviceLiveMatchSpan();
}

template<class Require> void verifyWholeMatchBlindReusesListen (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair();
    auto& post = host.post();
    post.setWholeMatchPreferred (true);
    require (post.wholeMatchPreferred() && post.startLiveCompare() == StartResult::started,
             "ALL PLAYED fixture enters ordinary LISTEN on the real POST");
    renderWholeMatchProductBlocks (host, 420);
    const auto listen = post.measureLiveCompare (true);
    require (listen.ok() && listen.wholeOwnerBound && listen.proofBound && listen.analysisUnits >= 3 * 48000u,
             "LISTEN really holds an eligible ALL PLAYED span before BLIND");
    const auto plan = planMatch (listen, 0.0);
    require (static_cast<bool> (post.applyLiveCompareMatch (plan,
                plan.needsApproval ? MatchChoice::lowerPost : MatchChoice::basis)),
             "LISTEN whole-span plan applies before entering BLIND");
    renderWholeMatchProductBlocks (host, 64);
    const auto approved = post.liveCompareStatus();
    const auto revision = LiveTimingFixtureAccess::gainRevision(post).load();
    const auto published = LiveTimingFixtureAccess::wholeSpanPublishedForTest(post);
    require (approved.matchReady && published >= 3 * 48000u, "LISTEN MATCH is fully applied");
    require (post.beginLiveBlind() == StartResult::started && post.liveBlindStatus().stage == BlindStage::preparing,
             "production BLIND starts a new timing proof, not a new whole measurement");
    require (!LiveTimingFixtureAccess::wholeSpanEnabledForTest(post),
             "there is no ALL PLAYED capture during BLIND preparation");
    for (int n = 0; n < 250 && post.liveBlindStatus().stage != BlindStage::active; ++n)
        renderWholeMatchProductBlocks (host, 1);
    const auto inherited = post.liveCompareStatus();
    require (post.liveBlindStatus().stage == BlindStage::active
        && LiveTimingFixtureAccess::gainRevision(post).load() == revision
        && inherited.gain == approved.gain && inherited.postTarget == approved.postTarget,
        "before three new seconds BLIND uses exactly the already approved gain, ceiling and attenuation revision");
    require (LiveTimingFixtureAccess::wholeSpanPublishedForTest(post) == published
        && !LiveTimingFixtureAccess::wholeSpanEnabledForTest(post),
        "same LISTEN accumulator never receives BLIND audio");
    post.finishLiveCompare();
    renderWholeMatchProductBlocks (host, 96);
}

template<class Require> void verifyBlindRefusesUnappliedListenRevision (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const bool whole : { false, true })
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair();
        auto& post = host.post();
        post.setWholeMatchPreferred (whole);
        require (post.startLiveCompare() == StartResult::started, "pending boundary starts real LISTEN");
        renderWholeMatchProductBlocks (host, 420);
        const auto measured = post.measureLiveCompare (true);
        require (measured.ok() && measured.wholeOwnerBound == whole,
                 "LISTEN measured the actual LAST 4 s or ALL PLAYED route selected by the case");
        const auto plan = planMatch (measured, 0.0);
        require (static_cast<bool> (post.applyLiveCompareMatch (plan,
            plan.needsApproval ? MatchChoice::lowerPost : MatchChoice::basis)),
            "LISTEN has approved a real complete MATCH");
        // No audio callback or artificial receipt between applying this plan and pressing BLIND.
        const auto pending = LiveTimingFixtureAccess::gainRevision (post).load();
        const auto before = post.liveCompareStatus();
        require (before.matched && !before.matchReady
            && LiveTimingFixtureAccess::gainReceipt (post) != pending,
            "the approved LISTEN revision has not settled on the audio thread at the press");
        require (post.beginLiveBlind() == StartResult::started
            && post.liveBlindStatus().stage == BlindStage::preparing,
            "BLIND starts through the normal processor method");
        require (!LiveTimingFixtureAccess::wholeSpanEnabledForTest (post),
                 "an unapplied revision does not start an ALL PLAYED measurement in BLIND");
        for (int n = 0; n < 560 && post.liveBlindStatus().stage != BlindStage::active; ++n)
            renderWholeMatchProductBlocks (host, 1);
        const auto chosen = LiveTimingFixtureAccess::gainRevision (post).load();
        require (post.liveBlindStatus().stage == BlindStage::active && chosen != pending
            && LiveTimingFixtureAccess::gainReceipt (post) == chosen,
            "BLIND reaches active only after applying a recent-window MATCH, never by a late LISTEN receipt");
        post.finishLiveCompare();
        renderWholeMatchProductBlocks (host, 96);
    }
}

template<class Require> void verifyDirectBlindUsesRecent (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair();
    auto& post = host.post();
    post.setWholeMatchPreferred(true);
    require(post.beginLiveBlind() == StartResult::started, "direct BLIND starts with ALL PLAYED preference saved");
    require(!LiveTimingFixtureAccess::wholeSpanEnabledForTest(post)
        && LiveTimingFixtureAccess::wholeSpanPublishedForTest(post) == 0,
        "direct BLIND never creates a whole-playback capture");
    for (int n=0; n<560 && post.liveBlindStatus().stage != BlindStage::active; ++n)
        renderWholeMatchProductBlocks(host,1);
    require(post.liveBlindStatus().stage == BlindStage::active
        && LiveTimingFixtureAccess::wholeSpanPublishedForTest(post) == 0,
        "the established recent measurement still prepares direct BLIND");
    post.finishLiveCompare();
    renderWholeMatchProductBlocks(host,96);
}

template<class Require> void verifyWholeMatchPreClockRace (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const bool advances : { false, true })
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair();
        auto& post = host.post();
        post.setWholeMatchPreferred (true);
        require (post.startLiveCompare() == StartResult::started, "clock race fixture starts ordinary ALL PLAYED LISTEN");
        renderWholeMatchProductBlocks (host, 420);
        const auto measured = post.measureLiveCompare (true);
        require (measured.ok() && measured.wholeOwnerBound && measured.proofBound && measured.generationBound,
                 "clock race fixture obtains a real proof- and owner-bound ALL PLAYED result");
        const auto base = post.liveCompareStatus();
        const auto held = base.postTarget > 0.0f ? 20.0 * std::log10 (base.postTarget) : 0.0;
        const auto plan = planMatch (measured, held);
        require (plan.failure == MatchFailure::none && plan.wholeOwnerBound && plan.proofBound,
                 "the measured plan is otherwise applicable");
        const auto choice = plan.needsApproval ? MatchChoice::lowerPost : MatchChoice::basis;
        const auto* publisher = LiveTimingFixtureAccess::publishedRing (host.pre1());
        const auto* reader = LiveTimingFixtureAccess::publishedRing (post);
        require (publisher != nullptr && reader != nullptr && publisher->ring() != nullptr
                     && reader->ring() != nullptr && publisher->key() == reader->key(),
                 "the real processors share this PRE ring");
        auto& source = publisher->ring()->header;
        const auto& observed = reader->ring()->header;
        const auto clock = observed.timing.generation.load (std::memory_order_acquire);
        require (source.timing.generation.load() == clock && observed.run.load() == measured.preRun,
                 "the POST still sees the measured clock and PCM run");
        if (advances)
        {
            // Deterministic publication boundary: PRE has advanced its clock but has not changed
            // the PCM run or owner, and POST has not rendered a new block. No sleeps or callbacks.
            source.timing.generation.fetch_add (1, std::memory_order_acq_rel);
            require (observed.timing.generation.load() == clock + 1 && observed.run.load() == measured.preRun
                         && measured.wholeOwner.same (gainIdentity (*reader->ring(), post.restoreOrPairTicket())),
                     "only the PRE clock changed; ownership and PCM run match the pending plan");
            require (! post.measureLiveCompare (true).ok(), "measurement no longer returns data for the old POST proof");
        }
        const auto applied = post.applyLiveCompareMatch (plan, choice);
        if (advances)
        {
            const auto after = post.liveCompareStatus();
            require (applied.failure == MatchFailure::stale && after.active && after.gain == base.gain
                         && after.postTarget == base.postTarget && after.matched == base.matched,
                     "a plan measured before the clock change is stale and cannot change either gain");
        }
        else
            require (static_cast<bool> (applied), "an otherwise identical plan applies with the original PRE clock");
        post.finishLiveCompare();
        renderWholeMatchProductBlocks (host, 96);
    }
}

template<class Require> void verifyWholeMatchSpanProduct (Require require)
{
    verifyWholeMatchBlindReusesListen (require);
    verifyBlindRefusesUnappliedListenRevision (require);
    verifyDirectBlindUsesRecent (require);
    verifyWholeMatchPreClockRace (require);
}

template<class Require> bool runWholeMatchSpanProductMode (std::string_view mode, Require require,
                                                            const juce::File& storage)
{
    if (mode != "--whole-match-span-product-only") return false;
    writeCandidateFixtureIdentity (require, storage);
    verifyWholeMatchSpanProduct (require);
    return true;
}
}
