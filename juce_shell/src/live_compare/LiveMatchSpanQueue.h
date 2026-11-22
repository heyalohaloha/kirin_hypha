#pragma once

#include "LiveCompareRing.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <limits>

namespace hypha::live_compare
{
// Realtime producer and message-thread consumer. A format/owner/session decision belongs to the
// caller; this queue neither certifies timing nor guesses an invalid pair. It publishes all of a
// proven block or none. Consumer stalls never cause a truncated span to be portrayed as "all".
class LiveMatchSpanQueue
{
public:
    struct Frame { float pre[2], post[2]; std::uint64_t epoch; bool gapBefore; };
    struct Staged { std::uint64_t end = 0; bool copied = false; };
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    explicit LiveMatchSpanQueue(std::uint64_t n) : capacity(n), pcm(std::make_unique<Frame[]>(n)) {}

    Staged stage(const Ring& ring, std::uint64_t run, std::int64_t start, float* const* post, int channels,
                 int frames, std::uint64_t epoch, bool gapBefore) noexcept
    { return stageObserved(ring, run, start, post, channels, frames, epoch, gapBefore, [] {}); }

    template<class EndCopyObserver>
    Staged stageObserved(const Ring& ring, std::uint64_t run, std::int64_t start, float* const* post,
        int channels, int frames, std::uint64_t epoch, bool gapBefore, EndCopyObserver&& endCopy) noexcept
    {
        if (epoch == 0 || frames <= 0 || channels < 1 || channels > 2) return {};
        const auto size = static_cast<std::uint64_t>(frames);
        const auto out = write.load(std::memory_order_relaxed);
        if (size > capacity || out - read.load(std::memory_order_acquire) > capacity - size) return {};
        const auto seq = ring.header.seq.load(std::memory_order_acquire);
        if ((seq & 1u) != 0 || start > std::numeric_limits<std::int64_t>::max() - frames
            || !readable(ring, run, start, frames)) return {};
        for (int i = 0; i < frames; ++i)
        {
            const auto slot = static_cast<std::size_t>((out + static_cast<std::uint64_t>(i)) % capacity);
            auto& frame = pcm[slot];
            for (int ch = 0; ch < channels; ++ch)
            {
                frame.pre[ch] = ring.samples[sampleSlot(start + i, static_cast<std::uint32_t>(ch))].load(std::memory_order_relaxed);
                frame.post[ch] = post[ch][i];
            }
            frame.epoch = epoch;
            frame.gapBefore = i == 0 && gapBefore;
        }
        endCopy(); // default compiles away; deterministic seqlock interruption fixture
        std::atomic_thread_fence(std::memory_order_acquire);
        if (ring.header.seq.load(std::memory_order_relaxed) != seq
            || ring.header.ownerClosed.load(std::memory_order_acquire) != 0) return {};
        return {out + size, true};
    }

    void finish(Staged block, std::uint64_t epoch) noexcept
    {
        if (block.copied) write.store(block.end, std::memory_order_release);
        else lastLost.store(epoch, std::memory_order_release);
    }

    std::uint64_t published() const noexcept { return write.load(std::memory_order_acquire); }
    void discardPublished() noexcept { read.store(published(), std::memory_order_release); }

    template<class Consumer>
    bool drainThrough(std::uint64_t target, std::uint64_t limit, Consumer&& consume)
    {
        auto at = read.load(std::memory_order_relaxed);
        if (target <= at) return true;
        const auto available = std::min(target - at, limit);
        for (std::uint64_t i = 0; i < available; ++i) consume(pcm[static_cast<std::size_t>((at + i) % capacity)]);
        read.store(at + available, std::memory_order_release);
        return at + available == target;
    }

    bool incomplete(std::uint64_t epoch) const noexcept
    {
        return epoch != 0 && lastLost.load(std::memory_order_acquire) == epoch;
    }

private:
    static bool readable(const Ring& ring, std::uint64_t run, std::int64_t start, int frames) noexcept
    {
        if (ring.header.ownerClosed.load(std::memory_order_relaxed) != 0
            || ring.header.run.load(std::memory_order_relaxed) != run) return false;
        const auto end = ring.header.writeEnd.load(std::memory_order_acquire);
        if (start < ring.header.runStart.load(std::memory_order_acquire) || end < start + frames) return false;
        return static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(start) <= ringCapacityFrames;
    }
    const std::uint64_t capacity;
    const std::unique_ptr<Frame[]> pcm;
    std::atomic<std::uint64_t> read {0}, write {0}, lastLost {0};
};
}
