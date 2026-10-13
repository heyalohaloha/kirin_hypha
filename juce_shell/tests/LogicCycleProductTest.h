#pragma once
#include "ContentAnalysisProductFixture.h"

// Full processor, real workers, actual MATCH and shared-memory transport. The portable clock
// fixture separately checks every PCM sample and all malformed callback combinations.
inline void verifyLogicCycleProduct (content_analysis_product::Fixture& fixture)
{
    using namespace content_analysis_product;
    using namespace hypha::live_compare;
    auto& post = *fixture.post;
    fixture.until ([&] { return post.setPairCandidate (fixture.pre->instanceId(), {}); }, "Logic pair");
    for (bool spectrum : { true, false })
    {
        hypha::analysis::Demand demand;
        demand.kind = spectrum ? hypha::analysis::Kind::spectrum : hypha::analysis::Kind::sharpDelta;
        require (post.setAnalysisDemand (fixture.owner, demand), "Logic analysis demand");
        fixture.until ([&] { return fixture.zeroDelta (spectrum); }, "Logic cycle delta");
        require (!post.analysisPairingLoopBlocked(), "proven cycle has no off guidance");
        require (post.startLiveCompare() == StartResult::started, "Logic cycle LISTEN");
        fixture.auditionAllowed = true; post.selectLiveComparePre (true);
        fixture.until ([&] { return post.liveCompareStatus().preAudible; }, "Logic correct PRE");
        require (post.beginLiveBlind() == StartResult::started, "Logic cycle BLIND prepares MATCH");
        fixture.until ([&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                       "Logic real MATCH and BLIND");
        int deltaBlocks = 0;
        // Span a complete non-block-aligned cycle; the anonymous trial stays uninterrupted.
        for (int n = 0; n < 1600; ++n)
        {
            fixture.block(); post.serviceLiveBlind();
            require (post.liveBlindStatus().stage == BlindStage::active, "cycle must not interrupt proven BLIND");
            if (fixture.zeroDelta (spectrum)) ++deltaBlocks;
        }
        require (deltaBlocks > 1500, "delta persists through the block-internal wrap");
        fixture.postClock.reported = false;
        fixture.block(); post.serviceLiveBlind(); fixture.noDelta (spectrum);
        require (post.liveBlindStatus().stage == BlindStage::invalidated, "one missing callback proof ends BLIND");
        fixture.postClock.reported = true;
        for (int n = 0; n < 32; ++n)
        {
            fixture.block(); post.serviceLiveBlind();
            require (post.liveBlindStatus().stage == BlindStage::invalidated
                && !post.liveCompareStatus().preAudible, "recovery never restarts BLIND");
        }
        post.finishLiveCompare();
        fixture.until ([&] { return !post.liveCompareStatus().finishing; }, "Logic END");
        fixture.auditionAllowed = false;
    }
}
