#pragma once
#include "ReaperFixture.h"

namespace reaper_boundary
{
using namespace reaper_fixture;

enum class Fault { preProject, postProject, preClock, postClock, badWrap, lap,
                   delay, preGap, postGap, seek, tempo, range, preRevision, postRevision };
inline void apply (Host& host, Fault fault)
{
    switch (fault)
    {
        case Fault::preProject: host.preProjectAvailable = false; break;
        case Fault::postProject: host.postProjectAvailable = false; break;
        case Fault::preClock: host.clockMissingSide = 0; break;
        case Fault::postClock: host.clockMissingSide = 1; break;
        case Fault::badWrap: host.preProjectShift = 128; break;
        case Fault::lap:
        {
            const auto target = host.prePosition + host.length;
            while (host.prePosition < target)
            {
                host.preFrames = static_cast<int> (std::min<std::int64_t> (frames, target - host.prePosition));
                host.publish();
            }
            host.preFrames = frames;
            break;
        }
        case Fault::delay: host.runtimeDelay = 4096; break;
        case Fault::preGap: host.gapSide = 0; break;
        case Fault::postGap: host.gapSide = 1; break;
        case Fault::seek: host.postProjectShift = 4096; break;
        case Fault::tempo: host.bpm = 121; break;
        case Fault::range: host.rangeDelta = 0.25; break;
        case Fault::preRevision: host.changedRevisionSide = 0; break;
        case Fault::postRevision: host.changedRevisionSide = 1; break;
    }
}
inline void negative()
{
    constexpr Fault faults[] { Fault::preProject, Fault::postProject, Fault::preClock,
        Fault::postClock, Fault::badWrap, Fault::lap, Fault::delay, Fault::preGap,
        Fault::postGap, Fault::seek, Fault::tempo, Fault::range, Fault::preRevision,
        Fault::postRevision };
    for (const auto length : { 383999, 384001 })
    for (const auto after : { false, true })
    for (const auto fault : faults)
    {
        Host host; host.length = length; host.runtimeBound = true;
        while (host.postPosition / length == 0
               && host.postPosition % length + frames <= length) host.consume();
        if (after) host.consume();
        require (host.report.preAudible && host.decision.kValid, "boundary fault starts with proven PRE and K");
        apply (host, fault);
        for (int n = 0; n < 2; ++n)
        {
            host.consume();
            if (host.report.verdict == Verdict::accepted || host.report.preAudible)
                std::fprintf (stderr, "boundary failure: length=%d after=%d fault=%d block=%d\n",
                              length, after, int (fault), n);
            require (host.report.verdict != Verdict::accepted && !host.report.preAudible,
                     "fault cannot use old K to admit PRE");
        }
        require (host.wrongFrames == 0, "fault cannot play a wrong occurrence");
    }
}

inline void callbackGrids()
{
    for (const auto length : { 383999, 384001 })
    for (const auto variable : { false, true })
    {
        Host host; host.length = length; host.runtimeBound = true; host.prePosition = 1;
        constexpr int preSizes[] { 127, 513, 1024, 256 };
        constexpr int postSizes[] { 512, 256, 1024, 127 };
        int n = 0; std::uint64_t accepted = 0, rejected = 0;
        float weight = 0.0f;
        while (host.postPosition < 2 * length + 4096)
        {
            host.postFrames = variable ? postSizes[n % 4] : frames;
            while (host.prePosition < host.postPosition + host.postFrames + 1024)
            {
                host.preFrames = variable ? preSizes[n % 4] : frames;
                host.publish(); ++n;
            }
            const auto position = host.postPosition;
            host.consume (false);
            if (host.report.verdict == Verdict::accepted)
            {
                ++accepted;
                require (!host.report.guardTripped, "fixture must not trip output guard");
                for (int f = 0; f < host.postFrames; ++f)
                {
                    weight = std::min (1.0f, weight + 1.0f / 480.0f);
                    for (int c = 0; c < host.channels; ++c)
                    {
                        const auto sample = Host::sample (position + f, c);
                        const auto expected = sample * 0.5f * (1.0f - weight) + sample * weight;
                        require (std::abs (host.output[c][f] - expected) < 1.0e-7f,
                                 "every accepted frame matches the current occurrence and fade");
                    }
                }
            }
            else { ++rejected; weight = 0.0f; } // Host checks every rejected sample is POST.
            require (host.wrongFrames == 0, "different callback grids never play a wrong occurrence");
        }
        require (accepted > 0, "different callback grids exercise accepted PCM");
        std::printf ("GRID length=%d variable=%d accepted=%llu rejected=%llu\n", length, variable,
                     static_cast<unsigned long long> (accepted), static_cast<unsigned long long> (rejected));
    }
}
}
