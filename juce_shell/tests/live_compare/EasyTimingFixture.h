#pragma once
#include "../../src/live_compare/LiveCompareSession.h"
#include "../../src/live_compare/CertifiedProjectAnalysisClock.h"
#include "../ProductTestClock.h"
#include <array>
#include <memory>

// Independent continuously changing PCM and a physical delay line. Project coordinates never
// construct expected audio; identical loop positions cannot hide a wrong occurrence.
namespace easy_timing
{
using namespace hypha::live_compare;
constexpr int frames = 512, delay = 4096;
constexpr float approvedGain = 2.0f;
constexpr std::uint64_t key = 4417;
struct Pair
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    PreFeeder feeder; TimingObserver observer; PostRenderer renderer; PostLevel level;
    NamedSelection selection; NamedReentry renewal; BlindSession blind;
    PairedAnalysisClock analysis; CertifiedProjectAnalysisClock projectAnalysis;
    std::array<std::array<float, frames>, 2> input {}, output {}, expected {};
    std::array<std::array<float, delay>, 2> line {};
    hypha::tests::ProductTestClock time;
    int host, channels, head = 0, verified = 0, delta = 0;
    std::int64_t position = 0, shift = 0;
    bool initial = true;
    float knob = 0.5f, inputLevel = 1.0f;
    Pair (int hostIn, int channelsIn, bool anonymous) : host (hostIn), channels (channelsIn)
    {
        ring->initialise (key, 48000); ring->header.demand.store (1);
        renderer.prepare (frames, 48000); level.configure (48000);
        selection.select (true);
        if (anonymous) blind.start (true);
    }
    static float sample (std::int64_t frame, int channel)
    {
        const auto token = static_cast<std::uint32_t> (frame + 1) * 0x9e3779b1u;
        return static_cast<float> ((token >> (channel ? 8 : 0)) & 0xffffffu) / 67108864.0f;
    }
    BlockClock block (bool pre) const
    {
        BlockClock b;
        b.clock = 100000 + position - (pre ? 0 : delay);
        b.project = position + shift - (pre ? 0 : delay);
        b.clockValid = b.projectValid = b.playing = true; b.frames = frames;
        b.wallNanos = time.nanos();
        b.clockBasis = static_cast<std::uint8_t> (host == 0 ? ClockBasis::vst3Continuous
            : host == 1 || host == 4 ? ClockBasis::audioUnitRender
            : host == 2 ? ClockBasis::aaxEngine : ClockBasis::reaperProcessing);
        b.clockAuthority = static_cast<std::uint8_t> (host == 0 ? ClockAuthority::certifiedContent
            : host == 1 ? ClockAuthority::logicRender : host == 2 ? ClockAuthority::boundedAaxEngine
            : host == 3 ? ClockAuthority::certifiedProject : ClockAuthority::none);
        b.maximumDelaySamples = host == 2 ? 16383 : 0;
        return b;
    }
    RenderReport step (int fault = 0)
    {
        auto pre = block (true), post = block (false);
        if (fault == 1) pre.clockValid = false;
        if (fault == 2) pre.playing = post.playing = false;
        if (fault == 3) pre.afterGap = post.afterGap = true;
        if (fault == 4) { shift += 1234; pre.project += 1234; post.project += 1234; }
        for (int f = 0; f < frames; ++f)
        {
            for (int c = 0; c < 2; ++c)
            {
                input[c][f] = sample (position + f, c) * inputLevel;
                expected[c][f] = line[c][head]; line[c][head] = input[c][f];
                output[c][f] = expected[c][f] * knob;
            }
            head = (head + 1) % delay;
        }
        const float* in[] { input[0].data(), input[1].data() };
        float* out[] { output[0].data(), output[1].data() };
        feeder.feed (*ring, pre, in, channels);
        TimingSnapshot peer;
        require (readTiming (ring->header.timing, peer), "fixture metadata coherent");
        const auto proof = observer.observe (peer, post, 48000, ringCapacityFrames);
        analysis.observeLocal (post, 48000, true);
        analysis.observePair (post, proof, qualifiedAaxPair (pre, post, proof)
            || qualifiedVst3Pair (pre, post, proof) || qualifiedAudioUnitPair (pre, post, proof));
        if (host == 3 ? projectAnalysis.observe (post, 48000, true).valid : analysis.paired.valid) ++delta;
        if (renewal.request (selection.command(), true, renderer.needsNewAdmission()))
        { renderer.renewNamedTiming(); initial = true; }
        if (initial && renderer.adoptInitialTiming (*ring, post, proof)) initial = false;
        const auto cmd = blind.command();
        const auto result = renderer.render (*ring, key, 48000, post, out, channels,
            true, approvedGain, level, 1.0f, 1.0f, cmd.active());
        if (result.timelineChanged) renewal.lost (result.loss, true);
        if (result.verdict == Verdict::accepted) renewal.accepted();
        if (cmd.active())
        {
            if (result.verdict != Verdict::accepted || result.guardTripped) blind.fallback (cmd, frames, 48000.0);
            else blind.observe (cmd, result.stableSource && result.preAudible);
            require (blind.valid (cmd), "timing loss preserves trial identity");
        }
        if (result.verdict != Verdict::accepted || result.stableSource || result.guardTripped)
            for (int c = 0; c < channels; ++c) for (int f = 0; f < frames; ++f)
                require (output[c][f] == expected[c][f] * (result.preAudible ? approvedGain : knob),
                         "every accepted PRE frame is correct; every unverified frame is POST");
        if (result.stableSource && result.preAudible) ++verified;
        position += frames; time.advanceBlock (frames, 48000);
        return result;
    }
};
inline void run()
{
    // Studio One/Pro, Logic AU linear fallback, Pro Tools AAX, REAPER and generic AU.
    for (int host = 0; host < 5; ++host)
    for (int channels : { 1, 2 })
    for (bool blind : { false, true })
    {
        Pair pair (host, channels, blind);
        for (int i = 0; i < 120; ++i) pair.step();
        require (pair.verified > 90, "each host really plays PRE");
        require (pair.delta > 90, "each host supplies real paired delta observations");
        for (int i = 0; i < 80; ++i)
        {
            const auto observations = pair.delta;
            pair.knob = i % 3 == 0 ? 0.0f : i % 3 == 1 ? 0.25f : 0.8f;
            require (pair.step().preAudible, "changed POST level does not change clock correspondence");
            require (pair.delta == observations + 1, "changed POST level retains paired clock observations");
        }
        bool ceilingSeen = false;
        pair.inputLevel = 8.0f;
        for (int i = 0; i < 40; ++i)
        {
            const auto result = pair.step();
            ceilingSeen = ceilingSeen || result.guardTripped;
            require (!result.guardTripped || !result.preAudible, "above-ceiling PRE is never output");
        }
        require (ceilingSeen, "level automation really exercises the fixed ceiling");
        pair.inputLevel = 1.0f;
        const auto beforeCeilingRecovery = pair.verified;
        for (int i = 0; i < 120; ++i) pair.step();
        require (pair.verified > beforeCeilingRecovery + 80, "safe PRE returns with unchanged approval after a level excursion");
        const auto epoch = pair.blind.command().epoch();
        for (int fault = 1; fault <= 4; ++fault)
        {
            const auto old = pair.renderer.historyView();
            require (!pair.step (fault).preAudible, "fault block is POST");
            const auto before = pair.verified;
            for (int i = 0; i < 120; ++i) pair.step();
            require (pair.verified > before + 80, "fresh proof resumes without selection or END");
            require (!pair.renderer.historyStillValid (old, old.start), "MATCH cannot join across a gap");
            if (blind) require (pair.blind.command().epoch() == epoch && pair.blind.view().usedPostFallback,
                                "same hidden assignment retains fallback fact");
        }
    }
}
}
