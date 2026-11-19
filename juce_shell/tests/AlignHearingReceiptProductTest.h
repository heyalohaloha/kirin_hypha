#pragma once
#include "AlignSessionProductTest.h"
#include <array>

namespace hypha::tests::align_hearing_receipt
{
template<class Require> void verify (Require require)
{
    using namespace live_compare;
    std::array<std::vector<bool>, 4> notices;
    std::array<int, 4> firstClear { -1, -1, -1, -1 };
    int run = 0;
    for (const bool firstPre : { false, true }) for (const int initialSource : { 1, 2 })
    {
        // One chunk is exactly one POST callback. Artificial estimator values test the real
        // publication/hearing/service path, not an independent PCM or estimator accuracy oracle.
        aax_analysis_product::ChunkedPost chain (2, 1024);
        chain.setDelay (512); chain.auditOutput = false;
        auto& post = *chain.post;
        aax_analysis_product::selectPreparedPair (chain, "receipt fixture selects its synthetic pair");
        require (post.beginLiveBlind() == StartResult::started, "receipt fixture starts BLIND");
        chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                     "receipt fixture prepares BLIND");
        LiveTimingFixtureAccess::assignBlind (post, firstPre);
        require (post.selectLiveBlind (initialSource), "select initial anonymous source");
        chain.run (80); LiveTimingFixtureAccess::deferContentMonitorForTest (post);
        require (LiveTimingFixtureAccess::confirmAlignmentForTest (post, 1200), "first estimate uses product publication");
        for (const int source : { initialSource, 3 - initialSource, initialSource })
        { require (post.selectLiveBlind (source), "prepare both sources"); chain.run (80); }
        auto& alignment = LiveTimingFixtureAccess::prepareAlignmentForTest (post);
        const auto context = LiveTimingFixtureAccess::alignmentContextForTest (post);
        require (!post.liveCompareAlignPending() && alignment.requestFor (context).frames == 1200
                 && !post.liveBlindStatus().contentUnapplied, "initial real receipts clear the old change");
        require (2400 - 1200 > 2 && LiveTimingFixtureAccess::confirmAlignmentForTest (post, 2400),
                 "new estimate exceeds the product publication tolerance");
        require (alignment.requestFor (context).frames == 2400 && post.liveBlindStatus().contentUnapplied,
                 "product actually publishes the correction and raises the notice");
        int block = 0;
        for (const int source : { initialSource, 3 - initialSource, initialSource })
        {
            require (post.selectLiveBlind (source), "listen to anonymous source");
            for (int n = 0; n < 80; ++n, ++block)
            {
                chain.chunk (false);
                const auto receipt = LiveTimingFixtureAccess::alignmentHearingForTest (post);
                const bool before = post.liveBlindStatus().contentUnapplied;
                LiveTimingFixtureAccess::serviceContentTiming (post); post.serviceLiveBlind();
                const bool after = post.liveBlindStatus().contentUnapplied;
                notices[run].push_back (after);
                if (block < 80) require (after, "one source alone cannot clear the notice");
                if (before && !after && firstClear[run] < 0)
                {
                    require (receipt.valid && receipt.command == LiveTimingFixtureAccess::alignmentHearingCommandForTest (post) && receipt.first >= 24000 && receipt.second >= 24000,
                             "first clear is backed by actual playback receipts from both sources");
                    firstClear[run] = block;
                    std::printf ("receipt run %d first clear block %d command %llu frames %llu/%llu\n", run, block,
                        (unsigned long long)receipt.command, (unsigned long long)receipt.first, (unsigned long long)receipt.second);
                }
            }
        }
        require (firstClear[run] >= 0, "notice really clears after both sources were played"); ++run;
    }
    for (int i = 1; i < 4; ++i)
        require (firstClear[i] == firstClear[0] && notices[i] == notices[0],
                 "both hidden assignments and starting sources clear on the same callback");
}
}
