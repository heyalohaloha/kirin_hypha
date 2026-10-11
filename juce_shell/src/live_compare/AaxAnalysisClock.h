#pragma once
#include "LiveCompareTimingWitness.h"

namespace hypha::live_compare
{
// AddClock is an engine coordinate, never an invented output-presentation latency.
// A local observation needs continuous engine/project movement from the host's engine clock.
inline bool continuousAaxClock (const BlockClock& block) noexcept
{
    return block.clockBasis == static_cast<std::uint8_t> (ClockBasis::aaxEngine)
        && block.playing && block.clockValid && block.projectValid && block.frames > 0 && ! block.afterGap;
}

// The measured host build additionally carries the certificate and its documented delay bound.
inline bool certifiedAaxClock (const BlockClock& block) noexcept
{
    return continuousAaxClock (block)
        && block.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::boundedAaxEngine)
        && block.maximumDelaySamples != 0;
}

// A difference additionally needs the exact PRE lifetime/generation and the SAME K proof used by
// LISTEN's timing preparation. A certified pair accepts any proven K within the documented bound,
// including an initial LOOP. Any other AAX host accepts only the ordinary linear project-time
// proof outside a loop: the same K that already decides which PRE samples LISTEN plays.
inline bool qualifiedAaxPair (const BlockClock& pre, const BlockClock& post,
                              const TimingEvidence& evidence) noexcept
{
    if (! evidence.valid || evidence.postClock != post.clock || evidence.preGeneration == 0
        || (evidence.ownerA == 0 && evidence.ownerB == 0) || evidence.k < 0)
        return false;
    if (certifiedAaxClock (pre) && certifiedAaxClock (post))
    {
        const auto bound = std::min (pre.maximumDelaySamples, post.maximumDelaySamples);
        return evidence.k <= static_cast<std::int64_t> (bound);
    }
    return continuousAaxClock (pre) && continuousAaxClock (post)
        && evidence.kind == LoopEntryKind::linear && ! post.loop.active && ! pre.loop.active;
}

struct AnalysisClockReading
{
    bool valid = false;
    std::int64_t start = 0;
    std::uint64_t revision = 0;
};

class AaxAnalysisClock
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
                // Bit 62 distinguishes this authority in the Rust runtime. Never wrap it.
                if (exhausted || revision >= (std::uint64_t (1) << 62) - 1)
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
        if (! exhausted && continuousAaxClock (block) && ! movement.broken && ! movement.waiting)
            local = localSlot.accept (block.clock, { 0, 0, generation, 0, block.maximumDelaySamples });
    }

    // qualified is qualifiedAaxPair() of the same callback's PRE snapshot, POST block and evidence.
    void observePair (const BlockClock& block, const TimingEvidence& evidence, bool qualified) noexcept
    {
        paired = {};
        std::int64_t start = 0;
        if (qualified && evidence.valid && evidence.postClock == block.clock && evidence.k >= 0
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
