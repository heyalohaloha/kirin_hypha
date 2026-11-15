#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

namespace hypha::live_compare
{
// One audio writer and one message-thread reader. A receipt is one command, correction generation
// and both anonymous counts; a torn read is discarded rather than waiting on the Audio Thread.
class BlindHearingFrames
{
public:
    struct Receipt { std::uint64_t generation = 0, command = 0, first = 0, second = 0; bool valid = false; };
    std::uint64_t generation() const noexcept { return revision.load (std::memory_order_acquire); }
    void beginChange() noexcept { revision.fetch_add (1, std::memory_order_acq_rel); }
    void endChange() noexcept { revision.fetch_add (1, std::memory_order_release); }

    // `common` follows PRE's path under either anonymous assignment; `actual` confirms that this
    // block really emitted the commanded source. After any break both assignments wait the same
    // transition time before counting, including a switch or a clock/bypass recovery.
    void observe (std::uint64_t gen, std::uint64_t cmd, int frames, int fadeFrames,
                  bool common, bool actual) noexcept
    {
        if ((gen & 1u) != 0 || revision.load (std::memory_order_acquire) != gen) return;
        if (gen != audioGeneration || (cmd >> 32) != (audioCommand >> 32))
        { audioGeneration = gen; audioFrames[0] = audioFrames[1] = 0; warmFrames = 0; }
        if (cmd != audioCommand) { audioCommand = cmd; warmFrames = 0; }
        if (! common || frames <= 0) { warmFrames = 0; return; }
        if (warmFrames < fadeFrames)
        { warmFrames = frames >= fadeFrames - warmFrames ? fadeFrames : warmFrames + frames; return; }
        if (! actual) { warmFrames = 0; return; }
        if (revision.load (std::memory_order_acquire) != gen) return;
        const auto count = static_cast<std::uint64_t> (frames);
        auto& heard = audioFrames[(cmd & 2u) != 0 ? 1 : 0];
        heard = heard > std::numeric_limits<std::uint64_t>::max() - count
            ? std::numeric_limits<std::uint64_t>::max() : heard + count;
        sequence.fetch_add (1, std::memory_order_acq_rel);
        publishedGeneration.store (gen, std::memory_order_relaxed);
        publishedCommand.store (cmd, std::memory_order_relaxed);
        publishedFirst.store (audioFrames[0], std::memory_order_relaxed);
        publishedSecond.store (audioFrames[1], std::memory_order_relaxed);
        sequence.fetch_add (1, std::memory_order_release);
    }

    Receipt read() const noexcept
    {
        const auto before = sequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0) return {};
        const auto gen = publishedGeneration.load (std::memory_order_relaxed);
        const auto cmd = publishedCommand.load (std::memory_order_relaxed);
        const auto first = publishedFirst.load (std::memory_order_relaxed);
        const auto second = publishedSecond.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        const auto after = sequence.load (std::memory_order_acquire);
        if (before != after || (gen & 1u) != 0 || revision.load (std::memory_order_acquire) != gen) return {};
        return { gen, cmd, first, second, before != 0 };
    }
private:
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "audio receipts must be lock-free");
    std::atomic<std::uint64_t> revision { 0 }, sequence { 0 };
    std::atomic<std::uint64_t> publishedGeneration { 0 }, publishedCommand { 0 }, publishedFirst { 0 }, publishedSecond { 0 };
    std::uint64_t audioGeneration = 0, audioCommand = 0, audioFrames[2] { 0, 0 };
    int warmFrames = 0; // Audio Thread only
};
}
