#pragma once
#include "../../src/live_compare/LiveCompareSession.h"
#include "../../src/live_compare/LiveBlindSession.h"
#include "../../src/live_compare/LiveCompareReentry.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace reaper_fixture
{
using namespace hypha::live_compare;
inline void require (bool ok, const char* why)
{ if (! ok) { std::fprintf (stderr, "REAPER: %s\n", why); std::abort(); } }
constexpr int frames = 512, rate = 96000;
constexpr std::uint64_t pair = 1234;
struct Host
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    ContinuousClock preCounter, postCounter;
    ReaperCallbackContinuity preGaps, postGaps;
    PreFeeder feeder;
    TimingObserver observer;
    PostRenderer renderer;
    PostLevel level;
    BlindSession blind;
    NamedSelection selection; NamedReentry reentry;
    std::array<std::array<float, 1024>, 2> input {}, output {};
    int preFrames = frames, postFrames = frames;
    int clockMissingSide = -1, gapSide = -1, changedRevisionSide = -1;
    std::int64_t preProjectShift = 0, postProjectShift = 0;
    double bpm = 120.0, rangeDelta = 0.0;
    std::int64_t prePosition = 0, postPosition = 0, loopStart = 0, length = 0;
    std::uint64_t now = 1000000000;
    BlockClock pre, post;
    TimingEvidence evidence;
    Decision decision;
    RenderReport report;
    bool selected = true, playing = true;
    bool preProjectAvailable = true, postProjectAvailable = true;
    bool checkAudio = true;
    bool wallGap = false;
    std::int64_t audioOffset = 0;
    std::uint64_t wrongFrames = 0;
    int channels = 2;
    float gain = 1.0f;
    std::uint32_t runtimeDelay = 0;
    bool runtimeBound = false, foreignPost = false;
    int expiredSide = -1, missingSide = -1;
    Host()
    {
        selection.select (true);
        ring->initialise (pair, rate); ring->header.demand.store (1);
        renderer.prepare (1024, rate); level.configure (rate);
        // Different local origins are deliberate, even with identical project coordinates.
        for (int i = 0; i < 3; ++i) preCounter.next (raw(), frames, true);
        for (int i = 0; i < 7; ++i) postCounter.next (raw(), frames, true);
    }
    static hypha::HostAuxiliaryClock raw()
    { return { 0, hypha::AuxiliaryClockSource::vst3Continuous, false }; }
    static float sample (std::int64_t position, int channel)
    {
        const auto token = static_cast<std::uint32_t> (position + 1) * 0x9e3779b1u;
        // 24 exact mantissa bits: mono must not alias a 65536-sample delay.
        return static_cast<float> ((token >> (channel == 0 ? 0 : 8)) & 0x00ffffffu) / 67108864.0f;
    }
    BlockClock block (ContinuousClock& counter, std::int64_t position, int count = frames)
    {
        const auto c = counter.next (raw(), count, true);
        BlockClock b;
        b.clock = c.samples; b.clockValid = c.valid; b.clockBasis = static_cast<std::uint8_t> (c.basis);
        b.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedProject);
        b.maximumDelaySamples = runtimeDelay;
        if (runtimeBound) { b.runtimeChainA = 12; b.runtimeChainB = 34; b.runtimeRevision = 1; }
        const int side = &counter == &preCounter ? 0 : 1;
        b.clockValid = b.clockValid && clockMissingSide != side;
        if (changedRevisionSide == side) ++b.runtimeRevision;
        b.runtimeExpired = expiredSide == side;
        if (missingSide == side) b.runtimeChainA = b.runtimeChainB = b.runtimeRevision = 0;
        if (foreignPost && side == 1) b.runtimeChainA ^= 1;
        b.frames = count; b.playing = playing;
        b.projectValid = &counter == &preCounter ? preProjectAvailable : postProjectAvailable;
        b.project = length && position >= loopStart ? loopStart + positiveModulo (position - loopStart, length) : position;
        b.project += side == 0 ? preProjectShift : postProjectShift;
        if (length) b.loop = { true, true, b.project / 48000.0, loopStart / 48000.0,
            (loopStart + length) / 48000.0 + rangeDelta, bpm };
        b.wallNanos = now; b.thread = (now % 20) + 1;
        b.wallGap = wallGap;
        b.afterGap = gapSide == side || (&counter == &preCounter ? preGaps : postGaps).breaks (b, wallGap, rate);
        return b;
    }
    void publish()
    {
        pre = block (preCounter, prePosition, preFrames);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < preFrames; ++i)
            input[c][i] = sample (prePosition + i, c);
        const float* ptr[] { input[0].data(), input[1].data() };
        feeder.feed (*ring, pre, ptr, channels); prePosition += preFrames;
    }
    void consume (bool writePeer = true)
    {
        if (writePeer) publish();
        post = block (postCounter, postPosition, postFrames);
        TimingSnapshot snapshot;
        require (readTiming (ring->header.timing, snapshot), "coherent fixture metadata");
        evidence = observer.observe (snapshot, post, rate, ringCapacityFrames);
        if (reentry.request (selection.command(), true, renderer.needsNewAdmission())) renderer.renewNamedTiming();
        renderer.adoptInitialTiming (*ring, post, evidence);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < postFrames; ++i)
            output[c][i] = sample (postPosition + audioOffset + i, c) * 0.5f;
        float* ptr[] { output[0].data(), output[1].data() };
        const auto cmd = blind.command();
        report = renderer.render (*ring, pair, rate, post, ptr, channels,
            selected && (!cmd.active() || blind.valid (cmd)), gain, level, 1.0f, 1.0f,
            cmd.active(), RecoveryReason::none, &decision);
        if (report.timelineChanged) reentry.lost (report.loss, true);
        if (report.verdict == Verdict::accepted) reentry.accepted();
        if (cmd.active() && report.verdict != Verdict::accepted)
        {
            if (endsComparison (report.reason)) blind.invalidate (cmd, report.reason);
            else blind.fallback (cmd);
        }
        if (report.stableSource && report.preAudible)
            for (int c = 0; c < channels; ++c) for (int i = 0; i < postFrames; ++i)
            {
                const bool correct = output[c][i] == sample (postPosition + audioOffset + i, c) * gain;
                wrongFrames += correct ? 0 : 1;
                if (checkAudio) require (correct, "every PRE frame is the correct occurrence");
            }
        if (report.verdict != Verdict::accepted)
            for (int c = 0; c < channels; ++c) for (int i = 0; i < postFrames; ++i)
                require (output[c][i] == sample (postPosition + audioOffset + i, c) * 0.5f, "unverified block is entirely POST");
        postPosition += postFrames; now += static_cast<std::uint64_t> (postFrames) * 1000000000ull / rate;
    }
    void warm()
    { for (int n = 0; n < 24; ++n) consume(); require (report.stableSource && report.preAudible, "linear PRE starts"); }
};
}
