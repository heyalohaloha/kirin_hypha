#pragma once
#include "LiveCompareKeptAlignTest.h"

static void clockChangeInsideABlockCostsNoPostBlock()
{
    using namespace live_compare_kept_test;
    for (bool blind : { false, true }) for (bool selectedPre : { false, true })
    {
        Session s; s.blind = blind; s.select (selectedPre); s.play (8); s.measure (1000); s.play (16);
        s.restart(); s.select (selectedPre); s.play (1); s.serviced = false;
        for (auto& l : s.listeners) l.alignment.arm();
        for (int i = 0; i < 300 && !s.listeners[0].alignment.budgetSpent(); ++i) s.play (1);
        require (s.listeners[0].alignment.budgetSpent() && s.listeners[1].alignment.budgetSpent(),
                 "late-alignment fixture spends its budget before the service answers");
        for (auto& l : s.listeners) s.service (l);
        s.k += 64; s.play (1);
        for (const auto& l : s.listeners)
            require (!l.waiting && l.address() == 0, "answered spent budget resolves the changed K before this block reads");
        if (blind) require (s.listeners[0].waiting == s.listeners[1].waiting
            && s.listeners[0].address() == s.listeners[1].address(), "late K address and wait are independent of BLIND assignment");
        for (auto& l : s.listeners) s.service (l);
        s.play (2);
        // Switching away lets a BLIND source adopt the retained address without moving audible PRE.
        if (blind) { s.select (!selectedPre); s.play (2); }
        for (const auto& l : s.listeners)
            require (l.owner.keptValue().frames == 1000 && l.address() == 1000 && l.told().notRemeasured,
                     "service-answered fallback preserves the retained correction and its notice");
    }
    for (bool blind : { false, true })
    {
        Session s; s.blind = blind; s.select (true); s.play (8); s.measure (1000); s.play (16);
        require (!s.listeners[0].alignment.budgetSpent() && s.listeners[0].address() == 1000,
                 "inside-budget fixture plays its confirmed correction");
        s.serviced = false; s.k += 64; s.play (1);
        require (s.listeners[0].waiting && s.listeners[0].address() == 1000,
                 "within the budget a changed clock still waits and never exposes K early");
    }
    for (int blockFrames : { 64, 128 })
    {
        aax_chunk_test::Options options; options.k = -512; options.candidate = true; options.postFrames = blockFrames;
        aax_chunk_test::Chain chain (options); chain.run (30);
        const auto& alignment = chain.postRenderer().alignState();
        for (int i = 0; i < 600 && !alignment.budgetSpent(); ++i) { alignment.arm(); chain.chunk(); }
        require (alignment.budgetSpent(), "real renderer spends the budget in small callbacks");
        chain.relocate (48000); chain.run (4); chain.confirmAlign ({ 96000 }); chain.run (12);
        require (chain.report.preAudible && alignment.budgetSpent(), "real renderer starts on spent K fallback");
        chain.confirmAlign ({ 256 }); chain.setK (options.k + 1024);
        int waits = 0, proven = 0, postOnly = 0; bool stable = false;
        for (int i = 0; i < 10; ++i)
            chain.chunk ([&] (int callback, BlockClock&) {
                if (callback > 0 && chain.decision.kValid && chain.decision.k == options.k + 1024
                    && chain.decision.verdict == Verdict::accepted)
                {
                    ++proven; if (!chain.report.preAudible) ++postOnly; if (chain.report.alignWaiting) ++waits;
                    require (chain.decision.preStart == chain.renderedClock - chain.decision.k,
                             "the first and later proven blocks actually read the current K address");
                    bool containsPre = false;
                    for (int frame = 0; frame < blockFrames; ++frame)
                        containsPre = containsPre || chain.outputSample (frame) != aax_chunk_test::postSample;
                    require (containsPre, "the proven changed-clock block contains PRE PCM, not only a cleared waiting flag");
                    stable = stable || (chain.report.stableSource && chain.report.preAudible);
                }
            });
        require (proven > 0 && waits == 0 && postOnly == 0 && stable,
                 "small callbacks keep no POST-only wait at the new proven K and finish the fade");
    }

}
