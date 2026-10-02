#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hypha::clock_diagnostic
{
struct Row
{
    std::int64_t project = 0, auxiliary = 0;
    double rate = 0;
    std::int64_t inputLatency = 0, outputLatency = 0;
    std::uint64_t hostNanoseconds = 0;
    std::int64_t todSamples = 0; // AAX engine observation, separate from native/content time
    std::int64_t addClockSamples = 0; // AAX algorithm-context counter, NOT TOD/native time
    double ppq = 0, bpm = 0, loopStart = 0, loopEnd = 0;
    std::uint32_t frames = 0, channels = 0, flags = 0;
    std::uint32_t firstLeft = 0, lastLeft = 0, firstRight = 0, lastRight = 0;
    std::uint32_t identityFirst = 0, identityLast = 0, identityFrames = 0, silentPrefix = 0, identityErrors = 0;
    std::uint8_t auxiliarySource = 0, presentationSource = 0;
};

// Diagnostic-only, one audio producer and one message-thread observer. Each published row is
// immutable for the lifetime of the instance. There is deliberately no reset/overwrite operation:
// exporting a prefix never races with the audio producer, even if the host keeps processing.
template<std::size_t Capacity = 65'536>
class ClockTrace
{
public:
    void start() noexcept { started.store (true, std::memory_order_release); }
    void append (const Row& row) noexcept
    {
        if (! started.load (std::memory_order_acquire) || cursor == Capacity) return;
        rows[cursor++] = row;
        published.store (cursor, std::memory_order_release);
    }
    std::size_t size() const noexcept { return published.load (std::memory_order_acquire); }
    static constexpr std::size_t capacity() noexcept { return Capacity; }
    const Row& operator[] (std::size_t index) const noexcept { return rows[index]; }
private:
    static_assert (std::atomic<std::size_t>::is_always_lock_free);
    static_assert (std::atomic<bool>::is_always_lock_free);
    std::array<Row, Capacity> rows {};
    std::size_t cursor = 0; // Audio Thread only.
    std::atomic<std::size_t> published { 0 };
    std::atomic<bool> started { false };
};
}
