#pragma once
#include "../../src/live_compare/LiveCompareSession.h"
#include "../host_clock_diagnostic/IdentitySignal.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

// Retained Windows Studio Pro 8.1.2 / VST3 failure, 2026-10-02: 48 kHz, 528 frames,
// native LOOP 288000, PRE output presentation 4096 / POST 0. The first tail observations
// are native=0, PPQ=-0.158666... at content clock 284192, then -0.136666... at 284720.
// This fixture's physical delay and unique emissions are independent of product K/PPQ.
namespace vst3_native_clamp_test
{
using namespace hypha::live_compare;
constexpr int frames = 528, rate = 48000, length = 288000, delay = 4096;
inline void check (bool ok, const char* message)
{
    if (! ok) { std::fprintf (stderr, "VST3 native clamp: %s\n", message); std::exit (1); }
}
inline BlockClock clock (std::int64_t native, std::int64_t continuous, std::int64_t raw,
                         std::int64_t loopStart, bool looping, unsigned presentation)
{
    BlockClock block;
    block.clock = continuous; block.project = native; block.frames = frames;
    block.playing = block.clockValid = block.projectValid = true;
    block.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
    block.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedContent);
    block.presentationSource = 1; block.outputPresentationValid = true;
    block.outputPresentationSamples = presentation;
    block.loop = { looping, looping, static_cast<double> (raw) / 24000.0,
        static_cast<double> (loopStart) / 24000.0,
        static_cast<double> (loopStart + length) / 24000.0, 120.0 };
    return block;
}
inline void continuation()
{
    unsigned audited = 0, clamps = 0;
    for (const auto nativeStart : { std::int64_t (0), std::int64_t (86016), std::int64_t (98304) })
    for (const bool initial : { false, true })
    for (const bool delayedDemand : { false, true })
    {
        auto ring = std::make_unique<Ring>(); ring->initialise (17, rate);
        ring->header.demand.store (delayedDemand ? 0 : 1);
        PreFeeder feeder; TimingObserver observer; Consumer consumer;
        std::array<std::array<float, frames>, 2> input {}, expected {}, output {};
        std::array<std::array<float, delay>, 2> physicalDelay {};
        std::size_t head = 0;
        const float* in[] { input[0].data(), input[1].data() };
        float* out[] { output[0].data(), output[1].data() };
        bool admitted = false;
        std::int64_t firstK = 0;
        const int demandIndex = delayedDemand ? (length + frames - 1) / frames : 0;
        for (int index = 0; index < 2300; ++index)
        {
            const auto emission = static_cast<std::int64_t> (index) * frames;
            const bool looping = initial || index >= 100;
            const auto preNative = nativeStart + (looping ? emission % length : emission);
            const auto rawPost = preNative - delay;
            const auto nativePost = looping ? std::max (nativeStart, rawPost) : rawPost;
            const auto pre = clock (preNative, emission, preNative, nativeStart, looping, delay);
            const auto post = clock (nativePost, emission - delay, rawPost, nativeStart, looping, 0);
            if (delayedDemand && index == demandIndex) ring->header.demand.store (1);
            for (int f = 0; f < frames; ++f)
            {
                hypha::clock_diagnostic::IdentitySignal::sample (
                    static_cast<std::uint32_t> (emission + f + 1), input[0][f], input[1][f]);
                for (int c = 0; c < 2; ++c)
                {
                    expected[c][f] = physicalDelay[c][head];
                    physicalDelay[c][head] = input[c][f];
                }
                head = (head + 1) % delay;
            }
            feeder.feed (*ring, pre, in, 2);
            TimingSnapshot snapshot;
            check (readTiming (ring->header.timing, snapshot), "coherent metadata fixture");
            const auto evidence = observer.observe (snapshot, post, rate, ringCapacityFrames);
            if (index < demandIndex) continue;
            if (! admitted && evidence.valid)
            {
                admitted = consumer.adoptInitialTiming (*ring, post, evidence);
                firstK = evidence.k;
            }
            const auto decision = consumer.process (*ring, 17, rate, post, out, 2);
            if (delayedDemand && index == demandIndex)
                check (admitted && decision.kValid && ! decision.timelineChanged
                    && decision.verdict == Verdict::beforeRun,
                    "clock-only anchor retains admission but NEVER manufactures pre-demand PCM");
            if (index < demandIndex + 32) continue;
            check (admitted && decision.verdict == Verdict::accepted && ! decision.timelineChanged,
                   "every normal wrap stays audible without another admission gesture");
            check (evidence.valid && evidence.k == firstK && decision.k == firstK,
                   "timing preparation and PCM retain the SAME original proof across clamps");
            for (int c = 0; c < 2; ++c) for (int f = 0; f < frames; ++f)
                check (output[c][f] == expected[c][f], "wrong interior sample or loop occurrence");
            ++audited;
            if (rawPost < nativeStart) ++clamps;
        }
    }
    check (clamps > 100, "fixture really exercises non-block-aligned clamped tails");
    std::printf ("VST3 native clamp: initial/late LOOP, delayed demand, 3 native starts, %u exact blocks, %u tails PASS\n",
                 audited, clamps);
}
inline void contradictoryProof()
{
    LoopAnchor anchor;
    anchor.loopKnown = true; anchor.loopSamples = length;
    anchor.loop = { true, true, 0.0, 0.0, 12.0, 120.0 };
    const auto pre = clock (288, 288288, 288, 0, true, delay);
    const auto post = clock (0, 284192, -3808, 0, true, 0);
    auto verified = post;
    check (corroborateLoop (anchor, post, post.clock, rate, verified, &pre)
        && verified.project == 284192, "retained first-wrap observation is exactly corroborated");
    for (int fault = 0; fault < 22; ++fault)
    {
        auto a = anchor; auto p = pre; auto b = post; auto start = post.clock;
        switch (fault)
        {
            case 0: a.loopSamples = 0; break;
            case 1: a.loopSamples += 3; break;
            case 2: b.project += 3; break;
            case 3: b.loop.ppq += 3.0 / 24000.0; break;
            case 4: b.loop.ppq -= 12.0; break;
            case 5: p.outputPresentationValid = false; break;
            case 6: b.outputPresentationValid = false; break;
            case 7: p.outputPresentationSamples = 3000; break;
            case 8: p.outputPresentationSamples = length; break;
            case 9: p.clockAuthority = 0; break;
            case 10: b.clockAuthority = 0; break;
            case 11: p.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender); break;
            case 12: b.presentationSource = 2; break;
            case 13: p.playing = false; break;
            case 14: b.loop.bpm += 1; break;
            case 15: start += length; break;
            case 16: b.loop.ppq = std::numeric_limits<double>::quiet_NaN(); break;
            case 17: b.clockValid = false; break;
            case 18: b.projectValid = false; break;
            case 19: b.afterGap = true; break;
            case 20: p.afterGap = true; break;
            case 21: b.playing = false; break;
        }
        verified = b;
        check (! corroborateLoop (a, b, start, rate, verified, &p),
               "missing/contradictory/cross-lap proof cannot normalize the boundary");
    }
    std::puts ("VST3 native clamp: 22 contradictory proof controls PASS");
}
inline void run() { continuation(); contradictoryProof(); }
}
