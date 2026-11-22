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
    struct Frame { float pre[2], post[2]; std::uint64_t epoch; };
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    explicit LiveMatchSpanQueue(std::uint64_t n) : capacity(n), pcm(std::make_unique<Frame[]>(n)) {}

    bool push(const Ring& ring, std::uint64_t run, std::int64_t start, float* const* post, int channels,
              int frames, std::uint64_t epoch) noexcept
    {
        if (epoch == 0 || frames <= 0 || channels < 1 || channels > 2) return false;
        const auto size = static_cast<std::uint64_t>(frames);
        const auto out = write.load(std::memory_order_relaxed);
        if (size > capacity || out - read.load(std::memory_order_acquire) > capacity - size)
            return lost(epoch);
        if (start > std::numeric_limits<std::int64_t>::max() - frames || ! readable(ring, run, start, frames))
            return lost(epoch);
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
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (! readable(ring, run, start, frames)) return lost(epoch);
        write.store(out + size, std::memory_order_release);
        return true;
    }

    template<class Consumer>
    void drain(std::uint64_t limit, Consumer&& consume)
    {
        auto at = read.load(std::memory_order_relaxed);
        const auto available = std::min(write.load(std::memory_order_acquire) - at, limit);
        for (std::uint64_t i = 0; i < available; ++i) consume(pcm[static_cast<std::size_t>((at + i) % capacity)]);
        read.store(at + available, std::memory_order_release);
    }

    bool empty() const noexcept { return write.load(std::memory_order_acquire) == read.load(std::memory_order_relaxed); }

    bool incomplete(std::uint64_t epoch) const noexcept
    {
        return epoch != 0 && lastLost.load(std::memory_order_acquire) == epoch;
    }

private:
    bool lost(std::uint64_t epoch) noexcept { lastLost.store(epoch, std::memory_order_release); return false; }
    static bool readable(const Ring& ring, std::uint64_t run, std::int64_t start, int frames) noexcept
    {
        if (ring.header.run.load(std::memory_order_acquire) != run) return false;
        const auto end = ring.header.writeEnd.load(std::memory_order_acquire);
        if (start < ring.header.runStart.load(std::memory_order_acquire) || end < start + frames) return false;
        return static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(start) <= ringCapacityFrames;
    }
    const std::uint64_t capacity;
    const std::unique_ptr<Frame[]> pcm;
    std::atomic<std::uint64_t> read {0}, write {0}, lastLost {0};
};
}
