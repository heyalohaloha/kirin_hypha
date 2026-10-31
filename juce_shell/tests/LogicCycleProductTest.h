#pragma once
#include "ContentAnalysisProductFixture.h"

// Full processor, real workers, actual MATCH and shared-memory transport. The portable clock
// fixture separately checks every PCM sample and all malformed callback combinations.
// One CTest item runs one part, so each item's wall watchdog bounds one cycle-spanning pass:
// LISTEN with the FREQ delta, LISTEN with the SHARP delta, or BLIND. Every part crosses the
// non-block-aligned cycle wrap at least once (the cycle is 1500.5 blocks, each loop 1600).
enum class LogicCyclePart { freqListen, sharpListen, blind };

inline void verifyLogicCycleProduct (content_analysis_product::Fixture& fixture, LogicCyclePart part)
{
    using namespace content_analysis_product;
    using namespace hypha::live_compare;
    auto& post = *fixture.post;
    const bool spectrum = part != LogicCyclePart::sharpListen;
    fixture.until ([&] { return post.setPairCandidate (fixture.pre->instanceId(), {}); }, "Logic pair");
    hypha::analysis::Demand demand;
    demand.kind = spectrum ? hypha::analysis::Kind::spectrum : hypha::analysis::Kind::sharpDelta;
    require (post.setAnalysisDemand (fixture.owner, demand), "Logic analysis demand");
    fixture.until ([&] { return fixture.zeroDelta (spectrum); }, "Logic cycle delta");
    require (!post.analysisPairingLoopBlocked(), "proven cycle has no off guidance");
    require (post.startLiveCompare() == StartResult::started, "Logic cycle LISTEN");
    fixture.auditionAllowed = true; post.selectLiveComparePre (true);
    fixture.until ([&] { return post.liveCompareStatus().preAudible; }, "Logic correct PRE");
    if (part != LogicCyclePart::blind)
    {
        int deltaBlocks = 0;
        // The actual paired delta must survive a complete non-block-aligned cycle in LISTEN.
        for (int n = 0; n < 1600; ++n)
        {
            fixture.block(); post.serviceLiveBlind();
            require (post.liveCompareStatus().preAudible, "cycle keeps verified named PRE");
            if (fixture.zeroDelta (spectrum)) ++deltaBlocks;
        }
        require (deltaBlocks > 1500, "delta persists through the block-internal wrap");
    }
    else
    {
        require (post.beginLiveBlind() == StartResult::started, "Logic cycle BLIND prepares MATCH");
        fixture.until ([&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                       "Logic real MATCH and BLIND");
        const auto command = LiveTimingFixtureAccess::command (post);
        require (post.selectLiveBlind (command.firstPre() ? 1 : 2), "select anonymous PRE");
        fixture.until ([&] { return post.liveCompareStatus().preAudible; }, "Logic anonymous PRE");
        const auto trial = post.liveBlindStatus().trial;
        // Blind deliberately suppresses paired analysis; the independent PCM oracle still runs
        // on every callback. Do not mistake that existing admission boundary for lost timing.
        for (int n = 0; n < 1600; ++n)
        {
            fixture.block(); post.serviceLiveBlind();
            require (post.liveBlindStatus().stage == BlindStage::active
                && post.liveBlindStatus().trial.epoch == trial.epoch, "cycle keeps the same anonymous trial");
        }
        fixture.postClock.reported = false;
        fixture.auditionAllowed = false;
        fixture.block(); post.serviceLiveBlind(); fixture.noDelta (spectrum);
        require (post.liveBlindStatus().stage == BlindStage::active && !post.liveCompareStatus().preAudible,
                 "missing presentation proof uses bit-identical POST without ending BLIND");
        // One 256-frame callback (5.3 ms) of POST is too brief to hear; REVEAL discloses a run of 20 ms or more.
        require (! post.liveBlindStatus().trial.usedPostFallback, "a single brief callback of POST is not disclosed");
        for (int n = 0; n < 4; ++n) { fixture.block(); post.serviceLiveBlind(); fixture.noDelta (spectrum); }  // 26.7 ms in all
        fixture.postClock.reported = true; fixture.auditionAllowed = true;
        fixture.until ([&] { post.serviceLiveBlind(); return post.liveCompareStatus().preAudible; },
                       "fresh Logic proof automatically resumes anonymous PRE");
        require (post.liveBlindStatus().trial.epoch == trial.epoch
            && post.liveBlindStatus().trial.usedPostFallback, "Logic recovery preserves assignment and fallback fact");
    }
    post.finishLiveCompare();
    fixture.until ([&] { return !post.liveCompareStatus().finishing; }, "Logic END");
    fixture.auditionAllowed = false;
}
