#pragma once
#include "LiveCompareBlockClock.h"
#include "LiveCompareLoopCycle.h"

namespace hypha::live_compare
{
// Supported *verified address* envelope, not a host-wide PDC guarantee. 65536 is the
// planned delay stress limit, 8192 the supported scheduling/block allowance. The
// measured 4096-delay maximum age (4608 at 96 kHz / 512) is not used as a bound.
// A known linear K permits checking this envelope in PRE's own counter. An initial
// loop has no such K and MUST NOT infer this bound from a modulo age or ring size.
struct ReaperTimingLimits
{
    static constexpr std::int64_t delay = 65536;
    static constexpr std::int64_t scheduling = 8192;
    static constexpr std::int64_t age = delay + scheduling;
};

inline bool reaperProcessing (const BlockClock& block) noexcept
{
    return block.clockBasis == static_cast<std::uint8_t> (ClockBasis::reaperProcessing)
        && block.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::certifiedProject);
}

// A burst pause is only a scheduling observation. Defer a raw wall gap to the pair
// check when this side's actual project movement and counter are continuous. A
// whole-lap pause cannot use repeated project positions as that witness.
class ReaperCallbackContinuity
{
public:
    bool breaks (const BlockClock& block, bool wallGap, double rate) noexcept
    {
        if (!reaperProcessing (block)) { havePrevious = false; cycles.reset(); return false; }
        const auto cycle = cycles.observe (block, rate);
        const bool linear = havePrevious && !previous.loop.active && !block.loop.active
            && block.project == previous.project + previous.frames;
        const bool wrap = havePrevious && cycle.known && !cycle.changed
            && loopPositionMatches (previous.loop, previous.project, previous.frames,
                                    block.loop, block.project, rate)
            && block.wallNanos >= previous.wallNanos
            && static_cast<double> (block.wallNanos - previous.wallNanos) * rate
                < static_cast<double> (cycle.samples) * 1.0e9;
        const bool continuous = havePrevious && previous.playing && block.playing
            && previous.clockValid && block.clockValid && previous.projectValid && block.projectValid
            && block.clock == previous.clock + previous.frames && (linear || wrap);
        previous = block; havePrevious = true;
        return wallGap && !continuous;
    }
private:
    BlockClock previous;
    LoopCycleMeter cycles;
    bool havePrevious = false;
};

// Snapshot is the existing coherent PRE timing publication. Sequence/generation,
// both counters and project movement all matter; merely raising a wall-gap timeout
// would admit a skipped side. No allocation, host API, wall-clock read or PCM scan.
class ReaperPairContinuity
{
public:
    template <typename Snapshot>
    TimelineBreak observe (const Snapshot& pre, const BlockClock& post) noexcept
    {
        if (!reaperProcessing (post)) { reset(); return TimelineBreak::none; }
        TimelineBreak loss = TimelineBreak::none;
        if (!pre.active || !reaperProcessing (pre.block)) loss = TimelineBreak::clockMissing;
        else if (havePrevious)
        {
            if (pre.ownerA != ownerA || pre.ownerB != ownerB) loss = TimelineBreak::sourceChanged;
            else if (pre.generation != generation) loss = pre.cause;
            else
            {
                const auto delta = pre.block.clock - preClock;
                const auto publications = pre.sequence >= sequence ? (pre.sequence - sequence) / 2 : 0;
                const bool same = pre.sequence == sequence && delta == 0;
                const bool advanced = pre.sequence > sequence && publications > 0
                    && delta >= static_cast<std::int64_t> (publications)
                    && delta <= static_cast<std::int64_t> (publications) * ReaperTimingLimits::scheduling;
                if (!same && !advanced) loss = TimelineBreak::callbackGap;
                if (loss == TimelineBreak::none && (post.wallGap || pre.block.wallGap)
                    && (!advanced || post.clock != postClock + postFrames))
                    loss = TimelineBreak::callbackGap;
                if (loss == TimelineBreak::none && !post.loop.active && !pre.anchor.loopKnown
                    && pre.block.project - preProject != delta)
                    loss = TimelineBreak::projectMoved;
            }
        }
        ownerA = pre.ownerA; ownerB = pre.ownerB; generation = pre.generation; sequence = pre.sequence;
        preClock = pre.block.clock; preProject = pre.block.project;
        postClock = post.clock; postFrames = post.frames;
        havePrevious = pre.active && post.playing;
        return loss;
    }
    void reset() noexcept { *this = {}; }
private:
    bool havePrevious = false;
    std::uint64_t ownerA = 0, ownerB = 0, generation = 0, sequence = 0;
    std::int64_t preClock = 0, preProject = 0, postClock = 0;
    std::int32_t postFrames = 0;
};
}
