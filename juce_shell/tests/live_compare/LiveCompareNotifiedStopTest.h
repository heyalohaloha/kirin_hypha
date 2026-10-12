#pragma once
#include "../../src/live_compare/LiveCompareSession.h"
#include <array>
#include <cstdio>
#include <memory>

namespace hypha::tests
{
template <typename Require>
void verifyNotifiedTransportStop (Require require)
{
    using namespace live_compare;
    constexpr int frames = 512;
    constexpr std::uint32_t rate = 96000;
    constexpr std::uint64_t key = 0x1234;
    auto ring = std::make_unique<Ring>(); ring->initialise (key, rate); ring->header.demand.store (1);
    PreFeeder feeder;
    PostRenderer renderer; renderer.prepare (frames, rate);
    PostLevel level; level.configure (rate);
    std::array<float, frames> preLeft, preRight, postLeft, postRight;
    preLeft.fill (0.1f); preRight.fill (0.1f);
    const float* pre[] { preLeft.data(), preRight.data() };
    float* post[] { postLeft.data(), postRight.data() };
    BlockClock block;
    block.frames = frames; block.clockValid = block.projectValid = block.playing = true;
    block.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
    const auto render = [&] {
        feeder.feed (*ring, block, pre, 2);
        postLeft.fill (0.2f); postRight.fill (0.2f);
        return renderer.render (*ring, key, rate, block, post, 2, true, 1.0f, level, 1.0f, 1.0f);
    };
    RenderReport report;
    for (int i = 0; i < 32; ++i)
    { report = render(); block.clock += frames; block.project += frames; }
    require (report.verdict == Verdict::accepted && report.preAudible, "normal PRE is proven before the stop");
    const auto previousRun = ring->header.run.load();
    // Neither side received a stopped render. Host stop/reset notification precedes a render
    // clock reset and a project rewind; no old K or stale PRE may sound in the restart block.
    block.clock = block.project = 0; block.afterGap = block.transportRestart = true;
    report = render();
    require (ring->header.run.load() != previousRun && report.loss == TimelineBreak::stopped
        && report.verdict != Verdict::accepted && ! report.preAudible, "explicit stop creates fresh PRE and POST timing origins");
    for (int i = 0; i < frames; ++i)
        require (postLeft[static_cast<std::size_t> (i)] == 0.2f && postRight[static_cast<std::size_t> (i)] == 0.2f,
                 "the unverified restart block outputs bit-identical POST");
    block.afterGap = block.transportRestart = false;
    for (int i = 0; i < 32; ++i)
    { block.clock += frames; block.project += frames; report = render(); }
    require (report.verdict == Verdict::accepted && report.preAudible, "PRE returns only after the new K is proven");
    block.clock += frames; block.project = 0; // ordinary playing seek, no stop flag
    report = render();
    require (report.loss == TimelineBreak::projectMoved && report.verdict != Verdict::accepted,
             "playing seek remains its own timing loss, never forged stop evidence");
    std::printf ("Notified transport stop: PASS (fresh runs, safe POST, new K, seek)\n");
}
}
