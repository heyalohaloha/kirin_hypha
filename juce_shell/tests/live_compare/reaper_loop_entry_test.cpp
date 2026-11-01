#include "ReaperFixture.h"
#include "LoopReviewRegressions.h"
#include "ReaperWrapDiagnostic.h"
#include "ReaperBoundaryFaults.h"
#include <string>
#include "../ProductTestClock.h"
using namespace reaper_fixture;
int main (int argc, char** argv)
{
    if (argc == 2 && std::string (argv[1]) == "--wrap-diagnostic-only") { diagnoseReaperWraps(); return 0; }
    reaper_boundary::negative();
    reaper_boundary::callbackGrids();
    loop_review::reaper();
    loop_review::absentTokenProof();
    loop_review::otherHosts();
    loop_review::copyOverlap();
    hypha::tests::ProductTestClock time;
    for (int channels : { 1, 2 })
    for (int delay : { 0, 4096, 65536 })
    {
        Host host; host.channels = channels;
        host.length = 384000;
        host.runtimeBound = true;
        host.runtimeDelay = static_cast<std::uint32_t> (delay);
        for (int i = 0; i < delay / frames; ++i) host.publish();
        bool heardInFirstLap = false;
        for (int i = 0; i < 1600; ++i)
        {
            host.now = time.nanos(); host.consume(); time.advanceBlock (frames, rate);
            if (host.report.preAudible && host.postPosition < host.length) heardInFirstLap = true;
            if (i == 300) host.blind.start (true);
            if (i > 300) require (!host.blind.view().invalidated, "first split wrap preserves proven BLIND");
        }
        require (heardInFirstLap, "runtime chain bound admits correct PRE in first lap");
        require (host.wrongFrames == 0, "no wrong occurrence");
    }
    for (int fault = 0; fault < 4; ++fault)
    {
        Host host; host.length = 384000; host.runtimeBound = true;
        for (int i = 0; i < 100; ++i) host.consume();
        require (host.report.preAudible, "fault fixture starts proven PRE");
        host.blind.start (true);
        if (fault == 0) host.runtimeBound = false; // expired/failed API observation
        if (fault == 1) host.runtimeDelay = 4096; // changed delay tuple
        if (fault == 2) host.postProjectAvailable = false;
        if (fault == 3) for (int i = 0; i < 750; ++i) host.publish(); // unexplained whole lap ahead
        host.consume();
        require (!host.report.preAudible && !host.blind.view().invalidated, "one unverified block uses POST without ending BLIND");
        host.runtimeBound = true; host.postProjectAvailable = true;
        for (int i = 0; i < 40; ++i) host.consume();
        require (!host.blind.view().invalidated && (fault == 3 ? !host.report.preAudible : host.report.preAudible), "fresh proof resumes; out-of-bound PRE remains withheld");
    }
    {
        Host host; host.length = 384000; host.runtimeBound = true;
        for (int i = 0; i < 100; ++i) host.consume();
        const auto oldRun = host.ring->header.run.load();
        host.runtimeDelay = 4096; host.publish();
        require (host.ring->header.run.load() != oldRun,
                 "changed chain evidence fences PCM and MATCH history at the same boundary");
    }
    for (int length : { 7680, 8191, 8192 })
    {
        Host host; host.length = length; host.runtimeBound = true;
        for (int i = 0; i < 200; ++i)
        {
            host.consume();
            require (!host.report.preAudible && host.report.verdict != Verdict::accepted,
                     "loop at or below the bound is never falsely unique");
        }
    }
    {
        Host host; host.length = 384000; host.runtimeBound = true;
        for (int i = 0; i < 50; ++i) host.consume();
        TimingSnapshot peer; require (readTiming (host.ring->header.timing, peer), "interval fixture");
        auto post = host.post;
        std::int64_t address = 0;
        require (reaperWindowAddress (peer.anchor, peer.block, post, rate, ringCapacityFrames, address), "covered interval");
        require (!reaperWindowAddress (peer.anchor, peer.block, post, rate, 8192, address), "capacity equals U is rejected");
        post.runtimeChainB ^= 1;
        require (!reaperWindowAddress (peer.anchor, peer.block, post, rate, ringCapacityFrames, address), "foreign chain cannot bind");
        post = host.post; peer.anchor.intervals.start = peer.anchor.intervals.end - 8191;
        require (!reaperWindowAddress (peer.anchor, peer.block, post, rate, ringCapacityFrames, address), "whole U coverage required");
        require (readTiming (host.ring->header.timing, peer), "restore boundary fixture");
        peer.block.clock = std::numeric_limits<std::int64_t>::max();
        require (!reaperWindowAddress (peer.anchor, peer.block, post, rate, ringCapacityFrames, address), "end overflow rejected");
        require (readTiming (host.ring->header.timing, peer), "restore offset fixture");
        post.clock = std::numeric_limits<std::int64_t>::min();
        require (!initialLoopCandidate (peer.anchor, peer.block, post, 0, rate, ringCapacityFrames).valid,
                 "candidate subtraction overflow rejected");
    }
    std::puts ("REAPER first-loop / split-wrap / burst output PASS");
}
