#pragma once
#include "LiveCompareRing.h"

#include <cstdint>

namespace hypha::live_compare
{
// INV-LC28. Content moves only the audition's PRE address. K remains the clocks' proof,
// including an arbitrary 64-bit difference between the instances' clock origins.
// A recalibrated origin changes K, not the content delay retained for this pair/format/owner.
struct AlignValue
{
    std::int64_t frames = 0;
    bool operator== (const AlignValue& other) const noexcept
    { return frames == other.frames; }
    bool operator!= (const AlignValue& other) const noexcept { return ! (*this == other); }
};

inline AlignValue alignValue (std::int64_t lagFrames) noexcept
{
    // PRE late is not available yet. Actual address/run/write bounds are checked by the reader.
    if (lagFrames >= -1 || lagFrames <= -static_cast<std::int64_t> (ringCapacityFrames)) return {};
    return { -lagFrames };
}

inline bool alignedReadStart (std::int64_t clock, std::int64_t k, AlignValue value, std::int64_t& start) noexcept
{ return checkedClockSubtract (clock, k, start) && checkedClockSubtract (start, value.frames, start); }

struct AlignContext
{
    std::uint64_t authority = 0, format = 0, ownerA = 0, ownerB = 0;
    bool operator== (const AlignContext& other) const noexcept
    { return authority == other.authority && format == other.format && ownerA == other.ownerA && ownerB == other.ownerB; }
    bool operator!= (const AlignContext& other) const noexcept { return ! (*this == other); }
};

inline AlignContext alignOwner (AlignContext context, const Ring& ring) noexcept
{
    context.ownerA = ring.header.timing.ownerA.load (std::memory_order_acquire);
    context.ownerB = ring.header.timing.ownerB.load (std::memory_order_acquire);
    return context;
}

struct AlignPlayback
{
    AlignContext context;
    bool enabled = true, outputAllowed = true;
};

// One non-RT writer publishes a complete value and the context captured BEFORE measuring.
// RT takes one bounded snapshot, retaining its current value only on publication contention.
class AlignRequest
{
public:
    void publish (AlignValue value, AlignContext context) noexcept
    {
        sequence.fetch_add (1);
        frames.store (value.frames);
        authority.store (context.authority); format.store (context.format);
        ownerA.store (context.ownerA); ownerB.store (context.ownerB);
        sequence.fetch_add (1);
    }
    AlignValue read (AlignContext context, AlignValue unchanged = AlignValue()) const noexcept
    {
        const auto before = sequence.load();
        if ((before & 1u) != 0) return unchanged;
        const AlignValue value { frames.load() };
        const AlignContext measured { authority.load(), format.load(), ownerA.load(), ownerB.load() };
        if (before != sequence.load()) return unchanged;
        return measured == context ? value : AlignValue {};
    }
private:
    std::atomic<std::uint64_t> sequence { 0 }, authority { 0 }, format { 0 }, ownerA { 0 }, ownerB { 0 };
    std::atomic<std::int64_t> frames { 0 };
};

#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
inline void (*afterAlignFormatChangeForTest)() = nullptr;
#endif
}
