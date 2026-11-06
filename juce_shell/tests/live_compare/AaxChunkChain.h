#pragma once
#include "../../src/live_compare/AaxChunkPosition.h"
#include "../../src/live_compare/LiveCompareChainTiming.h"
#include "../../src/live_compare/LiveCompareReentry.h"
#include "../../src/live_compare/LiveCompareSession.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

// An I/O insert between PRE and POST, as measured on Pro Tools 2026.4.1 at 48 kHz: PRE keeps running
// in 1024-frame blocks, one chunk ahead on the DAE clock; POST runs at the hardware buffer and sees
// one position per 1024 frames. Independent PCM per PRE clock sample: positions never build the
// expected audio, so an accepted block at any other address fails.
namespace aax_chunk_test
{
using namespace hypha::live_compare;
constexpr int chunkFrames = 1024;
constexpr std::uint32_t rate = 48000;
constexpr std::uint64_t key = 0xA11C0DEull;
constexpr float postSample = -0.25f; // never a PRE sample

// Broadband and zero-mean, independent per channel, like a mix: the content estimate needs one peak.
inline float pcm (std::int64_t clock, int channel) noexcept
{
    auto z = static_cast<std::uint64_t> (clock * 2 + channel) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (static_cast<float> (z >> 40) / 16777216.0f - 0.5f) * 0.2f;
}

inline void stamp (BlockClock& b, ClockBasis basis = ClockBasis::aaxEngine,
                   ClockAuthority authority = ClockAuthority::boundedAaxEngine, std::uint32_t bound = 16383) noexcept
{
    b.clockValid = b.projectValid = b.playing = true;
    b.clockBasis = static_cast<std::uint8_t> (basis);
    b.clockAuthority = static_cast<std::uint8_t> (authority);
    b.maximumDelaySamples = bound;
}

inline BlockClock aaxBlock (std::int64_t clock, std::int64_t project, int frames = 256) noexcept
{
    BlockClock b;
    b.clock = clock; b.project = project; b.frames = frames;
    stamp (b);
    return b;
}

struct Options
{
    int postFrames = 256;
    int preFrames = chunkFrames;        // below a chunk: PRE sees one position per chunk as well
    int prePhase = 0;                   // PRE's chunks start this many frames after POST's
    bool postLinear = false;            // POST reports every callback at postFrames
    bool preLinear = false;             // PRE reports every callback at preFrames
    bool adopt = true;                  // false: K comes from the renderer's own count alone
    bool record = false;                // keep every callback's decision
    std::int64_t k = -512;              // POST's clock minus PRE's for one project position
    bool named = true;                  // false: the measured failure, without AaxChunkPosition
    ClockBasis basis = ClockBasis::aaxEngine;
    ClockAuthority authority = ClockAuthority::boundedAaxEngine;
    std::uint32_t bound = 16383;
    bool preAfterPost = false;          // PRE runs after POST's chunk instead of one chunk ahead
    std::int64_t loopFrames = 0;
    bool preDemand = true;              // false: PRE publishes its clock and no PCM
    bool postCarriesPre = false;        // POST's input is PRE's audio contentK samples later
    std::int64_t contentK = 0;
};

class Chain
{
public:
    explicit Chain (Options optionsIn) : options (optionsIn), ring (std::make_unique<Ring>())
    {
        ring->initialise (key, rate); ring->header.demand.store (options.preDemand ? 1 : 0);
        renderer.prepare (chunkFrames, rate); level.configure (rate);
        selection.select (true);
        expectedK = options.k;
        // The first numbers of the measured run: POST 1068544 at -1252, PRE 1069568 at -740.
        postClock = 1068544; postHead = -1252;
        preClock = options.preAfterPost ? postClock : postClock + chunkFrames;
        preClock += options.prePhase;
        preProject = postHead + (preClock - postClock) + options.k;
        if (options.loopFrames > 0) { preProject = 5000; postHead = preProject - (preClock - postClock) - options.k; }
    }

    // One engine chunk. edit may change any raw POST callback the host delivers.
    template <typename Edit> void chunk (Edit edit)
    {
        if (! options.preAfterPost) feedPre();
        const auto callbacks = chunkFrames / options.postFrames;
        for (int i = 0; i < callbacks; ++i)
        {
            auto b = rawPost (postClock + i * options.postFrames);
            edit (i, b);
            post (b);
        }
        if (options.preAfterPost) feedPre();
        postClock += chunkFrames;
        postHead = wrapped (postHead + chunkFrames);
        ++chunks;
    }
    void chunk() { chunk ([] (int, BlockClock&) {}); }
    void run (int count) { for (int i = 0; i < count; ++i) chunk(); }

    // The host relocates both sides; delayChange also moves POST's compensated position.
    void relocate (std::int64_t by, std::int64_t delayChange = 0)
    {
        preProject += by; postHead += by - delayChange;
        expectedK = options.k + delayChange;
    }

    // The host's compensation moves POST's position relative to PRE's; K follows on proof only.
    void setK (std::int64_t k)
    {
        postHead = preProject - (preClock - postClock) - k;
        if (options.loopFrames > 0 && postHead < 0) postHead += options.loopFrames;
        expectedK = k;
    }
    void enterLoop (std::int64_t frames) { options.loopFrames = frames; }
    void leaveLoop() { options.loopFrames = 0; setK (expectedK); }

    void align (AlignValue value) noexcept { renderer.requestAlign (value, alignOwner ({}, *ring)); }
    void select (bool pre) noexcept { preSelected = pre; }
    void trial (bool on) noexcept { blindTrial = on; }

    const Ring& sharedRing() const noexcept { return *ring; }
    const PostRenderer& postRenderer() const noexcept { return renderer; }
    const auto& samples() const noexcept { return output; }
    std::int64_t renderedClock = 0;

    int accepted = 0, wrongAddress = 0, wrongPcm = 0, proven = 0, dropouts = 0, firstAcceptChunk = -1;
    int chunks = 0;
    Decision decision;
    RenderReport report;
    ChainTimingMeter chainMeter;
    std::vector<std::array<std::int64_t, 4>> log;

private:
    std::int64_t wrapped (std::int64_t position) const noexcept
    { return options.loopFrames > 0 && position >= options.loopFrames ? position % options.loopFrames : position; }

    void stampLoop (BlockClock& b) const noexcept
    {
        if (options.loopFrames <= 0) return;
        b.loop = { true, true, static_cast<double> (b.project) / 24000.0, 0.0,
                   static_cast<double> (options.loopFrames) / 24000.0, 120.0 };
    }

    BlockClock rawPost (std::int64_t clock) const noexcept
    {
        BlockClock b;
        b.clock = clock; b.frames = options.postFrames;
        b.project = options.postLinear ? wrapped (postHead + (clock - postClock)) : postHead;
        stamp (b, options.basis, options.authority, options.bound);
        stampLoop (b);
        return b;
    }

    void feedPre()
    {
        for (int offset = 0; offset < chunkFrames; offset += options.preFrames)
        {
            BlockClock b;
            b.clock = preClock + offset; b.frames = options.preFrames;
            b.project = options.preLinear ? wrapped (preProject + offset) : preProject;
            stamp (b, options.basis, options.authority, options.bound);
            stampLoop (b);
            b.wallNanos = wall; b.thread = 1;
            for (int i = 0; i < b.frames; ++i)
                for (int c = 0; c < 2; ++c)
                    input[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = pcm (b.clock + i, c);
            const float* in[] { input[0].data(), input[1].data() };
            if (options.named) prePosition.apply (b, rate);
            feeder.feed (*ring, b, in, 2);
        }
        preClock += chunkFrames;
        preProject = wrapped (preProject + chunkFrames);
    }

    void post (BlockClock b)
    {
        if (options.named) postPosition.apply (b, rate);
        wall += static_cast<std::uint64_t> (b.frames) * 1000000000ull / rate;
        b.wallNanos = wall; b.thread = 1;
        TimingSnapshot peer;
        if (! readTiming (ring->header.timing, peer)) std::abort();
        chainMeter.observe (peer, b, ring->header.published.load());
        const auto proof = observer.observe (peer, b, rate, ringCapacityFrames);
        if (renewal.request (selection.command(), true, renderer.needsNewAdmission()))
        { renderer.renewNamedTiming(); initial = true; }
        if (initial && options.adopt && renderer.adoptInitialTiming (*ring, b, proof)) initial = false;
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < b.frames; ++f)
                output[static_cast<std::size_t> (c)][static_cast<std::size_t> (f)]
                    = options.postCarriesPre ? pcm (b.clock - options.contentK + f, c) : postSample;
        float* out[] { output[0].data(), output[1].data() };
        decision = {};
        report = renderer.render (*ring, key, rate, b, out, 2, preSelected, 1.0f, level, 1.0f, 1.0f,
                                  blindTrial, RecoveryReason::none, &decision);
        renderedClock = b.clock;
        if (report.timelineChanged) renewal.lost (report.loss, true);
        if (options.record)
            log.push_back ({ static_cast<std::int64_t> (report.verdict), decision.kValid ? 1 : 0, decision.k,
                             static_cast<std::int64_t> (report.loss) });
        if (report.verdict == Verdict::accepted)
        {
            renewal.accepted();
            ++accepted;
            if (firstAcceptChunk < 0) firstAcceptChunk = chunks;
            if (decision.preStart != b.clock - expectedK - renderer.alignFrames()) ++wrongAddress;
            if (report.stableSource && report.preAudible)
            {
                ++proven;
                for (int c = 0; c < 2; ++c)
                    for (int f = 0; f < b.frames; ++f)
                        if (output[static_cast<std::size_t> (c)][static_cast<std::size_t> (f)] != pcm (decision.preStart + f, c)) ++wrongPcm;
            }
        }
        else if (firstAcceptChunk >= 0) ++dropouts;
    }

    Options options;
    std::unique_ptr<Ring> ring;
    AaxChunkPosition prePosition, postPosition;
    PreFeeder feeder; TimingObserver observer; PostRenderer renderer; PostLevel level;
    NamedSelection selection; NamedReentry renewal;
    bool initial = true, preSelected = true, blindTrial = false;
    std::int64_t preClock = 0, preProject = 0, postClock = 0, postHead = 0, expectedK = 0;
    std::uint64_t wall = 1000000000ull;
    std::array<std::array<float, chunkFrames>, 2> input {}, output {};
};
}
