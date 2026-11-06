#pragma once
#include "AaxChunkProductFixture.h"
#include <functional>

namespace aax_analysis_product
{
inline std::function<void()> formatInterleave;

inline void alignmentRejectsChangingFormat (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    auto& post = *chain.post;
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select format fixture PRE");
    require (post.startLiveCompare() == StartResult::started, "format fixture LISTEN");
    post.selectLiveComparePre (true);
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "format fixture settles");
    require (std::abs (post.liveCompareAlignMs() - 1024.0 * 1000.0 / 48000.0) < 1.0e-9,
             "display converts aligned frames at the prepared rate");
    post.prepareToPlay (48000, ChunkedPost::chunkFrames);
    require (post.liveCompareAlignFrames() == 1024, "same-format prepare keeps the display");
    const auto before = post.measureLiveCompareOffset();
    require (offsetCanVote (before), "old history is measurable before prepare");
    OffsetMonitor monitor;
    monitor.observe (before, false);
    const auto step = monitor.observe (before, false);
    bool visited = false, measured = true, published = true;
    double displayed = -1.0;
    formatInterleave = [&] {
        visited = true;
        require (LiveTimingFixtureAccess::alignmentHistoryValid (post), "interleave is before old history retires");
        std::thread service ([&] {
            displayed = post.liveCompareAlignMs();
            measured = post.measureLiveCompareOffset().determined;
            published = post.alignLiveCompare (step, before);
        });
        service.join();
    };
    afterAlignFormatChangeForTest = [] { formatInterleave(); };
    post.prepareToPlay (44100, ChunkedPost::chunkFrames);
    afterAlignFormatChangeForTest = nullptr;
    formatInterleave = {};
    require (displayed == 0.0, "display cannot read frames or rate during a format write");
    require (post.liveCompareAlignMs() == 0.0 && post.liveCompareAlignFrames() == 0,
             "completed format cannot pair old frames with its new rate before another callback");
    require (visited && ! measured && ! published, "prepare's new epoch cannot measure or publish old PCM");
    post.prepareToPlay (48000, ChunkedPost::chunkFrames);
    LiveTimingFixtureAccess::configureAaxClock (post);
    LiveTimingFixtureAccess::enableFixtureWrites (post);
    chain.chunk (false);
    LiveTimingFixtureAccess::serviceTiming (post);
    require (post.startLiveCompare() == StartResult::started, "LISTEN restarts in the completed format");
    post.selectLiveComparePre (true);
    chain.postScale = 0.0f;
    chain.run (220);
    require (post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 0,
             "undetermined new content cannot inherit the previous format's frames");
    chain.clean ("format interleave never emits a stale address");
}

inline void alignmentBelongsToPreOwner (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    auto& post = *chain.post;
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "select owner fixture PRE");
    require (post.startLiveCompare() == StartResult::started, "owner fixture LISTEN");
    post.selectLiveComparePre (true);
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 1024; }, "old PRE settles");
    const auto old = post.measureLiveCompareOffset();
    const auto identity = chain.pre->instanceId();
    chain.replacePre();
    require (chain.pre->instanceId() == identity, "only PRE's owner changes, its instance ID remains");
    chain.run (4);
    require (post.startLiveCompare() == StartResult::started, "same paired PRE can start with a new owner");
    post.selectLiveComparePre (true);
    const auto* mapping = LiveTimingFixtureAccess::publishedRing (post);
    require (mapping != nullptr && mapping->ring() != nullptr, "new PRE ring is open");
    const auto& timing = mapping->ring()->header.timing;
    require (timing.ownerA.load() != old.alignContext.ownerA || timing.ownerB.load() != old.alignContext.ownerB,
             "same instance ID has a different ring owner");
    LiveTimingFixtureAccess::publishMeasuredAlign (post, old); // a delayed old publication
    chain.postScale = 0.0f;
    chain.run (220);
    require (post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 0,
             "new PRE with ambiguous content cannot reuse the previous owner's correction");
    chain.postScale = 0.5f;
    chain.contentK = 1536;
    chain.until (400, [&] { return post.liveCompareAlignFrames() == 2048; }, "new owner measures its own correction");
    chain.clean ("PRE replacement never reads an old correction");
}
}
