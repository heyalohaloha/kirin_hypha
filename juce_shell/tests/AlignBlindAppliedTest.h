#pragma once
#include <vector>
#include "AlignChainProductTest.h"

namespace aax_analysis_product
{
// BLIND, INV-LC28: an effect that reports no delay is added between PRE and POST while the listener
// stays on one source. Two agreeing windows settle the new position, but PRE's read position moves
// only while the listener is away from PRE: a listener who stays on PRE hears the earlier position.
// The label therefore stays, with the same words at the same chunks whichever source is PRE and
// whichever is selected, until the listener has heard both sources again; then the correction plays
// and the label ends, again at the same chunk for every assignment.
inline void blindTellsAddedDelayUntilApplied (int channels)
{
    using namespace hypha::live_compare;
    constexpr std::int64_t added = 12000;
    const auto tracks = hypha::tests::chain_music::render (1068544 - 250000, 1800000, { 12.0, 12.0, 240, -12.0 });
    struct Run { bool firstPre; int source; };
    const Run runs[] = { { true, 1 }, { false, 2 }, { true, 2 } }; // PRE under both assignments, then POST
    std::vector<int> told[3];
    for (int r = 0; r < 3; ++r)
    {
        const bool pre = (runs[r].source == 1) == runs[r].firstPre;
        ChunkedPost chain (channels);
        chain.music = &tracks;
        chain.setDelay (512);
        auto& post = *chain.post;
        selectPreparedPair (chain, "select the chain before the effect is added");
        require (post.beginLiveBlind() == StartResult::started, "BLIND starts");
        chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; }, "BLIND prepares");
        LiveTimingFixtureAccess::assignBlind (post, runs[r].firstPre);
        require (post.selectLiveBlind (runs[r].source), "select one anonymous source");
        chain.run (400);
        const auto label = [&] { const auto s = post.liveBlindStatus(); return (s.contentUnmeasured ? 1 : 0) + (s.contentUnapplied ? 2 : 0); };
        require (label() == 0 && post.liveCompareStatus().preAudible == pre, "the settled position plays without a label");
        chain.contentK = 512 + added;
        int firstTold = -1;
        for (int i = 0; i < 400; ++i) // 8.5 s on the same source
        {
            chain.chunk();
            post.serviceLiveBlind();
            told[r].push_back (label());
            if (firstTold < 0 && told[r].back() != 0) firstTold = i;
            require (post.liveCompareStatus().preAudible == pre, "the selected source keeps playing");
            require (firstTold < 0 || told[r].back() != 0 || (pre && post.liveCompareAlignFrames() == added),
                     "while the listener stays on one source the label stays: PRE may still play at the earlier position");
        }
        require (firstTold >= 0 && firstTold * 1024 <= 4 * 48000, "the added delay is told within four seconds");
        require (! pre || post.liveCompareAlignFrames() == 0, "BLIND does not move PRE under the listener");
        require (post.liveBlindStatus().contentUnapplied, "the settled delay is told as not applied yet, not as unmeasured");
        for (const int source : { 3 - runs[r].source, runs[r].source })
        {
            require (post.selectLiveBlind (source), "the listener switches sources");
            for (int i = 0; i < 40; ++i) { chain.chunk(); post.serviceLiveBlind(); told[r].push_back (label()); }
        }
        require (label() == 0, "after both sources were heard the label ends");
        require (! pre || (post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == added),
                 "PRE plays with the correction after the listener's switch");
        chain.clean ("every PRE sample in BLIND is at the address the renderer claims while the delay is found");
    }
    int differing = 0, shown = 0;
    for (size_t i = 0; i < told[0].size(); ++i)
    {
        differing += told[0][i] != told[1][i] || told[0][i] != told[2][i] ? 1 : 0;
        shown += told[0][i] != 0 ? 1 : 0;
    }
    std::printf ("BLIND delay added while playing: label shown in %d chunks, differing between the assignments in %d\n", shown, differing);
    require (shown > 0 && differing == 0, "every assignment and selection shows the same label at the same time");
}
}
