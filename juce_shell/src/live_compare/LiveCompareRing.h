#pragma once

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
constexpr std::uint32_t ringVersion = 2;
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
    BlockDescriptor descriptors[ringDescriptors];
};

struct Ring
{
    RingHeader header;
    std::atomic<float> samples[std::size_t (ringCapacityFrames) * ringChannels];

    // Non-RT owner, before PRE publishes: stamps an empty ring for one pair and sample rate.
    void initialise (std::uint64_t pairKey, std::uint32_t sampleRate, std::uint32_t source = 0) noexcept
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
            && h.pairKey.load (std::memory_order_relaxed) == pairKey;
    }
};

inline std::size_t sampleSlot (std::int64_t clock, std::uint32_t channel) noexcept
{
    // Two's-complement wrap keeps negative clocks on a consistent slot.
    return std::size_t (static_cast<std::uint64_t> (clock) & (ringCapacityFrames - 1)) * ringChannels
         + channel;
}

// The clocks of one host callback as seen by one side of the pair.
struct BlockClock
{
    std::int64_t clock = 0;   // continuous clock of the first frame (host or plug-in frame count)
    std::int64_t project = 0; // host project position of the first frame
    std::int32_t frames = 0;
    bool clockValid = false;
    bool projectValid = false;
    bool playing = false;
    bool afterGap = false;    // the host did not call this side for a while before this block
};

// PRE, Audio Thread. A run starts whenever the clock does not continue the previous block or the
// host did not call PRE for a while; POST accepts only ranges written in the current run.
class Publisher
{
public:
    // Writes the block (a mono input is written to both channels). Returns true for a new run.
    bool publish (Ring& ring, const BlockClock& block, const float* const* input, int inputChannels) noexcept
    {
        auto& h = ring.header;
        if (! block.clockValid || block.frames <= 0 || input == nullptr || inputChannels <= 0)
        {
            haveRun = false; // the next valid block must open a new run
            return false;
        }
        const bool newRun = ! haveRun || block.clock != runEnd || block.afterGap;
        const auto seq = h.seq.load (std::memory_order_relaxed);
        h.seq.store (seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        if (newRun)
        {
            h.run.store (h.run.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            h.runStart.store (block.clock, std::memory_order_relaxed);
            h.writeEnd.store (block.clock, std::memory_order_relaxed);
        }
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
        h.published.store (count + 1, std::memory_order_release);
        h.seq.store (seq + 2, std::memory_order_release);
        haveRun = true;
        runEnd = block.clock + block.frames;
        return newRun;
    }

    void reset() noexcept { haveRun = false; }

private:
    bool haveRun = false;
    std::int64_t runEnd = 0;
};
}
