#pragma once
#include "CandidateProductHost.h"

namespace hypha::tests
{
template<class Require> void verifyLiveCompareRestartAlignment (Require require)
{
    using namespace live_compare;
    for (const int channels : { 1, 2 })
    {
        ProductTestClock wall;
        CandidateHost<Require> host (wall, require, channels, 512);
        host.delayA = 0; host.delayB = 0; host.pair();
        auto& post = host.post();
        post.setLiveCompareAutoAlign (true);
        require (post.startLiveCompare() == StartResult::started, "ordinary LISTEN starts");
        host.render (800, false, false);
        auto correction = post.measureLiveCompareOffset(); correction.lagFrames = -1545;
        LiveTimingFixtureAccess::publishMeasuredAlign (post, correction);
        host.render (3, false, false);
        require (post.liveCompareAlignFrames() == 1545, "ordinary LISTEN applies correction");
        const auto verifyOutput = [&]
        {
            post.selectLiveComparePre (true);
            host.render (100, false, true, false);
            require (post.liveCompareStatus().preAudible, "ordinary PRE is audible");
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < host.block; ++i)
                    require (std::abs (host.audio.getSample (ch, i)
                        - candidateInput (host.cursor - host.block + i - 1545, ch)) < 3.0e-6f,
                        "ordinary PRE output uses the independently indexed correction");
        };
        verifyOutput();
        post.finishLiveCompare(); host.render (10, false, false, false);
        require (post.startLiveCompare() == StartResult::started, "ordinary LISTEN restarts");
        require (!post.candidateCompareActive() && !post.candidateMeasurementIsolated(),
                 "ordinary restart does not enter or isolate A/B");
        host.render (800, false, false); // each session confirms its own content
        correction = post.measureLiveCompareOffset(); correction.lagFrames = -1545;
        LiveTimingFixtureAccess::publishMeasuredAlign (post, correction);
        verifyOutput();
    }
}
}
