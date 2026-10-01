#pragma once
#include "LiveCompareTimingWitness.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hypha::live_compare
{
// Live PRE/POST compare transport. PRE writes its raw input, indexed by its continuous clock,
// into a preallocated ring shared with the POST of the same pair. POST reads only ranges it can
// prove belong to PRE's current run (LiveCompareCorrespondence.h). Every header field, descriptor
// field and sample is a lock-free atomic, so PRE and POST running concurrently is race-free rather
// than merely detected. The layout is the same in both processes of a pair and never resized.
constexpr std::uint32_t ringMagic = 0x4B4C4331u; // "KLC1"
constexpr std::uint32_t ringVersion = 5;
constexpr std::uint32_t ringChannels = 2;
constexpr std::uint32_t ringCapacityFrames = 1u << 19; // power of two: about 10.9 s at 48 kHz
constexpr std::uint32_t ringDescriptors = 1024;
constexpr std::uint32_t joinSearchDepth = 256; // latest PRE blocks searched for a project-time join
constexpr std::uint32_t ringSourceMultiMono = 1u; // PRE is one channel of an AAX multi-mono set (INV-LC9)

static_assert ((ringCapacityFrames & (ringCapacityFrames - 1)) == 0);
static_assert (joinSearchDepth <= ringDescriptors);
static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
static_assert (std::atomic<std::int64_t>::is_always_lock_free);
static_assert (std::atomic<std::uint32_t>::is_always_lock_free);
static_assert (std::atomic<std::int32_t>::is_always_lock_free);
static_assert (std::atomic<float>::is_always_lock_free);
static_assert (std::atomic<double>::is_always_lock_free);

enum DescriptorFlag : std::uint32_t
{
    descriptorPlaying = 1u,
    descriptorProjectValid = 2u
};

// One PRE block: the clocks of its first frame. POST joins its own project time against these.
struct BlockDescriptor
{
    std::atomic<std::uint64_t> seq { 0 }; // odd while PRE rewrites this descriptor
    std::atomic<std::int64_t> clock { 0 };
    std::atomic<std::int64_t> project { 0 };
    std::atomic<std::int32_t> frames { 0 };
    std::atomic<std::uint32_t> flags { 0 };
};

struct RingHeader
{
    std::atomic<std::uint32_t> magic { 0 };
    std::atomic<std::uint32_t> version { 0 };
    std::atomic<std::uint32_t> channels { 0 };
    std::atomic<std::uint32_t> capacityFrames { 0 };
    std::atomic<std::uint32_t> sampleRate { 0 };
    std::atomic<std::uint32_t> source { 0 };  // PRE's input, ringSourceMultiMono or 0; fills the padding
    std::atomic<std::uint64_t> pairKey { 0 }; // POST refuses a ring stamped for another pair
    std::atomic<std::uint32_t> demand { 0 };  // POST sets it while a live session wants PRE input
    std::atomic<std::uint32_t> ownerClosed { 0 }; // PRE sets it as it unmaps; POST's session ends
    std::atomic<std::uint64_t> seq { 0 };     // odd while PRE writes samples or the run fields
    std::atomic<std::uint64_t> run { 0 };
    std::atomic<std::int64_t> runStart { 0 };
    std::atomic<std::int64_t> writeEnd { 0 };
    std::atomic<std::uint64_t> published { 0 };
    std::atomic<std::uint64_t> timingGeneration { 0 }; // PCM's PRE generation, covered by seq
    // One continuity-checked anchor, not a descriptor/PCM history expansion. These fields are
    // covered by seq and valid only in this run. A loop anchor never calibrates an initial K.
    std::atomic<std::int64_t> runProject { 0 }, loopClock { 0 }, loopProject { 0 };
    std::atomic<std::uint32_t> anchorFlags { 0 }; // bit 0: linear origin; bit 1: loop anchor
    std::atomic<double> loopPpq { 0 }, loopStart { 0 }, loopEnd { 0 }, loopBpm { 0 };
    TimingHeader timing; // clock-only preparation; never authorises output or claims PCM exists
    BlockDescriptor descriptors[ringDescriptors];
};

struct Ring
{
    RingHeader header;
    std::atomic<float> samples[std::size_t (ringCapacityFrames) * ringChannels];

    // Non-RT owner, before PRE publishes: stamps an empty ring for one pair and sample rate.
    void initialise (std::uint64_t pairKey, std::uint32_t sampleRate, std::uint32_t source = 0,
                     std::uint64_t ownerA = 0, std::uint64_t ownerB = 0) noexcept
    {
        auto& h = header;
        h.magic.store (0, std::memory_order_relaxed);
        h.version.store (ringVersion, std::memory_order_relaxed);
        h.channels.store (ringChannels, std::memory_order_relaxed);
        h.capacityFrames.store (ringCapacityFrames, std::memory_order_relaxed);
        h.sampleRate.store (sampleRate, std::memory_order_relaxed);
        h.source.store (source, std::memory_order_relaxed);
        h.pairKey.store (pairKey, std::memory_order_relaxed);
        h.demand.store (0, std::memory_order_relaxed);
        h.ownerClosed.store (0, std::memory_order_relaxed);
        h.seq.store (0, std::memory_order_relaxed);
        h.run.store (0, std::memory_order_relaxed);
        h.runStart.store (0, std::memory_order_relaxed);
        h.writeEnd.store (0, std::memory_order_relaxed);
        h.published.store (0, std::memory_order_relaxed);
        h.timingGeneration.store (0, std::memory_order_relaxed);
        h.anchorFlags.store (0, std::memory_order_relaxed);
        h.timing.flags.store (0, std::memory_order_relaxed);
        h.timing.generation.store (0, std::memory_order_relaxed);
        h.timing.sequence.store (0, std::memory_order_relaxed);
        // Raw in-process fixtures use a local serial. SharedRingMapping supplies a fresh
        // 128-bit identity, so a newly created PRE cannot inherit another mapping's evidence.
        static std::atomic<std::uint64_t> localOwners { 0 };
        h.timing.ownerA.store (ownerA != 0 ? ownerA : localOwners.fetch_add (1) + 1, std::memory_order_relaxed);
        h.timing.ownerB.store (ownerB != 0 ? ownerB : 1, std::memory_order_relaxed);
        for (auto& d : h.descriptors)
        {
            d.seq.store (0, std::memory_order_relaxed);
            d.flags.store (0, std::memory_order_relaxed);
        }
        h.magic.store (ringMagic, std::memory_order_release);
    }

    bool matches (std::uint64_t pairKey, std::uint32_t sampleRate) const noexcept
    {
        const auto& h = header;
        return h.magic.load (std::memory_order_acquire) == ringMagic
            && h.version.load (std::memory_order_relaxed) == ringVersion
            && h.channels.load (std::memory_order_relaxed) == ringChannels
            && h.capacityFrames.load (std::memory_order_relaxed) == ringCapacityFrames
            && h.sampleRate.load (std::memory_order_relaxed) == sampleRate
            && h.ownerClosed.load (std::memory_order_acquire) == 0
            && h.pairKey.load (std::memory_order_relaxed) == pairKey;
    }
};

inline std::size_t sampleSlot (std::int64_t clock, std::uint32_t channel) noexcept
{
    // Two's-complement wrap keeps negative clocks on a consistent slot.
    return std::size_t (static_cast<std::uint64_t> (clock) & (ringCapacityFrames - 1)) * ringChannels
         + channel;
}

// PRE, Audio Thread. A run starts whenever the clock does not continue the previous block or the
// host did not call PRE for a while; POST accepts only ranges written in the current run.
class Publisher
{
public:
    // Writes the block (a mono input is written to both channels). Returns true for a new run.
    bool publish (Ring& ring, const BlockClock& block, const float* const* input, int inputChannels) noexcept
    {
        if (! block.clockValid || block.frames <= 0 || input == nullptr || inputChannels <= 0)
        { haveRun = false; return false; }
        return publishAfterClock (ring, block, input, inputChannels,
            timeline.observe (block, ring.header.sampleRate.load (std::memory_order_relaxed)));
    }

    void reset() noexcept { haveRun = false; timeline.reset(); }

private:
    friend class PreFeeder;
    // PreFeeder owns one clock observer across idle/active callbacks. The exact same block's
    // result is reused for PCM; an active audition must not check PRE's timeline twice. Raw
    // Publisher fixtures retain the public path and their independent timeline above.
    bool publishAfterClock (Ring& ring, const BlockClock& block, const float* const* input,
                           int inputChannels, const TimelineStep& continuity) noexcept
    {
        auto& h = ring.header;
        if (! block.clockValid || block.frames <= 0 || input == nullptr || inputChannels <= 0)
        {
            haveRun = false; // the next valid block must open a new run
            return false;
        }
        const bool newRun = ! haveRun || block.clock != runEnd || continuity.broken
            || (wasLooping && ! block.loop.active);
        const auto seq = h.seq.load (std::memory_order_relaxed);
        h.seq.store (seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        if (newRun)
        {
            h.run.store (h.run.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            h.runStart.store (block.clock, std::memory_order_relaxed);
            h.writeEnd.store (block.clock, std::memory_order_relaxed);
            h.runProject.store (block.project, std::memory_order_relaxed);
            h.anchorFlags.store (block.playing && block.projectValid && ! block.loop.active ? 1u : 0u,
                                 std::memory_order_relaxed);
        }
        if (block.loop.active && (newRun || ! wasLooping) && block.loop.usable (h.sampleRate.load (std::memory_order_relaxed)))
        {
            h.loopClock.store (block.clock, std::memory_order_relaxed);
            h.loopProject.store (block.project, std::memory_order_relaxed);
            h.loopPpq.store (block.loop.ppq, std::memory_order_relaxed);
            h.loopStart.store (block.loop.start, std::memory_order_relaxed);
            h.loopEnd.store (block.loop.end, std::memory_order_relaxed);
            h.loopBpm.store (block.loop.bpm, std::memory_order_relaxed);
            h.anchorFlags.store (h.anchorFlags.load (std::memory_order_relaxed) | 2u, std::memory_order_relaxed);
        }
        wasLooping = block.loop.active;
        for (std::uint32_t channel = 0; channel < ringChannels; ++channel)
        {
            const auto* source = input[std::min (static_cast<int> (channel), inputChannels - 1)];
            for (std::int32_t i = 0; i < block.frames; ++i)
                ring.samples[sampleSlot (block.clock + i, channel)].store (source[i], std::memory_order_relaxed);
        }
        const auto count = h.published.load (std::memory_order_relaxed);
        auto& d = h.descriptors[count % ringDescriptors];
        const auto dseq = d.seq.load (std::memory_order_relaxed);
        d.seq.store (dseq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        d.clock.store (block.clock, std::memory_order_relaxed);
        d.project.store (block.project, std::memory_order_relaxed);
        d.frames.store (block.frames, std::memory_order_relaxed);
        d.flags.store ((block.playing ? descriptorPlaying : 0u)
                           | (block.projectValid ? descriptorProjectValid : 0u),
                       std::memory_order_relaxed);
        d.seq.store (dseq + 2, std::memory_order_release);
        h.writeEnd.store (block.clock + block.frames, std::memory_order_relaxed);
        h.timingGeneration.store (h.timing.generation.load (std::memory_order_acquire), std::memory_order_relaxed);
        h.published.store (count + 1, std::memory_order_release);
        h.seq.store (seq + 2, std::memory_order_release);
        haveRun = true;
        runEnd = block.clock + block.frames;
        return newRun;
    }

    bool haveRun = false;
    std::int64_t runEnd = 0;
    LoopTimeline timeline;
    bool wasLooping = false;
};
}
