#pragma once
#include "AaxAnalysisClock.h"

namespace hypha::live_compare
{
inline bool certifiedVst3Clock (const BlockClock& block) noexcept
{
    return block.clockBasis == static_cast<std::uint8_t> (ClockBasis::vst3Continuous)
        && block.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::certifiedContent)
        && block.playing && block.clockValid && block.projectValid && block.frames > 0 && ! block.afterGap;
}

inline bool qualifiedVst3Pair (const BlockClock& pre, const BlockClock& post,
                               const TimingEvidence& evidence) noexcept
{
    // The existing certificate identifies a shared content coordinate, not an engine counter.
    // In this coordinate K must be zero; contradictory linear evidence cannot override it.
    return certifiedVst3Clock (pre) && certifiedVst3Clock (post) && evidence.valid
        && evidence.postClock == post.clock && evidence.preGeneration != 0
        && (evidence.ownerA != 0 || evidence.ownerB != 0) && evidence.k == 0
        && (evidence.kind == LoopEntryKind::linear || evidence.kind == LoopEntryKind::contentClock);
}

// Render timestamps are local coordinates, not a certificate of content alignment. The same
// callback's LISTEN observer must prove the offset from an independent host project timeline.
inline bool continuousAudioUnitClock (const BlockClock& block) noexcept
{
    return block.clockBasis == static_cast<std::uint8_t> (ClockBasis::audioUnitRender)
        && block.playing && block.clockValid && block.projectValid && block.frames > 0 && ! block.afterGap;
}

inline bool qualifiedAudioUnitPair (const BlockClock& pre, const BlockClock& post,
                                    const TimingEvidence& evidence) noexcept
{
    if (! continuousAudioUnitClock (pre) || ! continuousAudioUnitClock (post)
        || ! evidence.valid || evidence.postClock != post.clock || evidence.preGeneration == 0
        || (evidence.ownerA == 0 && evidence.ownerB == 0)) return false;
    // A previously linear proof may survive a corroborated loop. Initial-loop proof requires
    // the existing positive latency difference and unique occurrence; no AU host is certified.
    return evidence.kind == LoopEntryKind::linear
        || (evidence.kind == LoopEntryKind::presentationLatency && pre.loop.active && post.loop.active
            && pre.presentationSource == 2 && post.presentationSource == 2
            && pre.outputPresentationValid && post.outputPresentationValid
            && pre.outputPresentationSamples > post.outputPresentationSamples);
}

struct AnalysisClockReading
{
    bool valid = false;
    std::int64_t start = 0;
    std::uint64_t revision = 0;
};

class PairedAnalysisClock
{
    struct Identity
    {
        std::uint64_t ownerA = 0, ownerB = 0, generation = 0;
        std::int64_t k = 0;
        std::uint32_t bound = 0;
        bool same (const Identity& other) const noexcept
        {
            return ownerA == other.ownerA && ownerB == other.ownerB && generation == other.generation
                && k == other.k && bound == other.bound;
        }
    };
    struct Slot
    {
        AnalysisClockReading accept (std::int64_t start, Identity next) noexcept
        {
            if (revision == 0 || ! identity.same (next))
            {
                // Reserve the clock-kind bits used by Rust. Never wrap proof revisions.
                if (exhausted || revision >= (std::uint64_t (1) << 60) - 1)
                { exhausted = true; return {}; }
                ++revision;
                identity = next;
            }
            return { true, start, revision };
        }
        Identity identity;
        std::uint64_t revision = 0;
        bool exhausted = false;
    };
public:
    void observeLocal (BlockClock block, double rate, bool eligible) noexcept
    {
        local = paired = {};
        if (! eligible) block.clockValid = false;
        const auto movement = timeline.observe (block, rate);
        const bool changed = ! havePrevious || ! sameClockProof (previous, block)
            || ! (rate <= previousRate && rate >= previousRate);
        if (movement.broken || changed)
        {
            if (generation == std::numeric_limits<std::uint64_t>::max()) exhausted = true;
            else ++generation;
        }
        previous = block; previousRate = rate; havePrevious = true;
        if (! exhausted && (continuousAaxClock (block) || certifiedVst3Clock (block) || continuousAudioUnitClock (block)) && ! movement.broken && ! movement.waiting)
            local = localSlot.accept (block.clock, { 0, 0, generation, 0, block.maximumDelaySamples });
    }

    // qualified belongs to the same callback's PRE snapshot, POST block and evidence.
    void observePair (const BlockClock& block, const TimingEvidence& evidence, bool qualified) noexcept
    {
        paired = {};
        std::int64_t start = 0;
        if (qualified && evidence.valid && evidence.postClock == block.clock
            && (evidence.k >= 0 || block.clockBasis == static_cast<std::uint8_t> (ClockBasis::audioUnitRender))
            && checkedClockSubtract (block.clock, evidence.k, start))
            paired = pairSlot.accept (start, { evidence.ownerA, evidence.ownerB,
                evidence.preGeneration, evidence.k, block.maximumDelaySamples });
    }

    AnalysisClockReading local, paired; // callback-local, never persisted or read by the UI
private:
    LoopTimeline timeline;
    BlockClock previous;
    double previousRate = 0;
    std::uint64_t generation = 0;
    bool havePrevious = false, exhausted = false;
    Slot localSlot, pairSlot;
};
}
