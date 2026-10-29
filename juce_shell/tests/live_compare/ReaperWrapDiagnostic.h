#pragma once
#include "ReaperFixture.h"
#include "../ProductTestClock.h"

inline void diagnoseReaperWraps()
{
    using namespace reaper_fixture;
    std::uint64_t totalFallback = 0, totalMixed = 0;
    for (const auto length : { 383999, 384000, 384001 })
    for (const auto delay : { 0, 4096 })
    for (const auto extraLead : { 0, 512, 4096 })
    {
        Host host;
        host.length = length; host.runtimeBound = true;
        host.runtimeDelay = static_cast<std::uint32_t> (delay);
        const auto lead = delay + extraLead;
        for (int i = 0; i < lead / frames; ++i) host.publish();
        hypha::tests::ProductTestClock clock;
        std::uint64_t fallback = 0, mixed = 0, wraps = 0, fallbackBlocks = 0, unverifiedPre = 0;
        bool heard = false;
        for (int i = 0; i < 2400; ++i)
        {
            const auto at = host.postPosition;
            host.now = clock.nanos(); host.consume(); clock.advanceBlock (frames, rate);
            if (i == 100) host.blind.start (true);
            if (i <= 100) continue; // Separate admission from the measured steady loop wraps.
            require (!host.blind.view().invalidated, "proven loop keeps the Blind selection");
            heard |= host.report.preAudible;
            const bool crosses = at / length != (at + frames - 1) / length;
            wraps += at / length != (at + frames) / length ? 1 : 0;
            unsigned postFrames = 0, mixedFrames = 0;
            for (int f = 0; f < frames; ++f)
            {
                const float oracle = Host::sample (at + f, 0);
                const float output = host.output[0][f];
                if (output == oracle * 0.5f) ++postFrames;
                else if (output != oracle) ++mixedFrames;
                if (host.report.verdict != Verdict::accepted && output != oracle * 0.5f) ++unverifiedPre;
            }
            fallback += postFrames; mixed += mixedFrames;
            if (postFrames || mixedFrames)
            {
                TimingSnapshot timing;
                require (readTiming (host.ring->header.timing, timing), "coherent interval witness");
                if (fallbackBlocks < 6)
                    std::printf ("WRAP_BLOCK length=%d delay=%d lead=%d at=%lld crosses=%d POST=%u mixed=%u verdict=%d loss=%d loopFailure=%d coverage=%lld upper=%lld\n",
                        length, delay, lead, static_cast<long long> (at), crosses, postFrames, mixedFrames,
                        int (host.report.verdict), int (host.report.loss), int (host.decision.loopFailure),
                        static_cast<long long> (timing.anchor.intervals.end - timing.anchor.intervals.start),
                        static_cast<long long> (reaperRuntimeAge (timing.block, host.post)));
                ++fallbackBlocks;
            }
        }
        std::printf ("WRAP_SUMMARY length=%d delay=%d lead=%d wraps=%llu fallbackBlocks=%llu POST=%llu mixed=%llu wrong=%llu unverifiedPRE=%llu heard=%d\n",
            length, delay, lead, static_cast<unsigned long long> (wraps), static_cast<unsigned long long> (fallbackBlocks),
            static_cast<unsigned long long> (fallback), static_cast<unsigned long long> (mixed),
            static_cast<unsigned long long> (host.wrongFrames), static_cast<unsigned long long> (unverifiedPre), heard);
        require (heard && host.wrongFrames == 0 && unverifiedPre == 0, "independent PCM occurrence and fail-closed output oracle");
        totalFallback += fallback; totalMixed += mixed;
    }
    std::fflush (stdout);
    require (totalFallback == 0 && totalMixed == 0, "fully supplied unique loop occurrences must not insert unnecessary POST frames");
}
