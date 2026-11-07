#pragma once
#include "kirin_hypha_ffi.h"
#include "live_compare/AaxChunkPosition.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace hypha::space_live
{
struct Sample { float left = 0.0f, right = 0.0f; };
struct Snapshot
{
    std::vector<Sample> samples;
    double sampleRate = 0.0;
    std::uint64_t revision = 0, span = 0, callbackNanos = 0, lifetimeNanos = 0;
    bool expired (std::uint64_t now) const noexcept
    { return now != 0 && callbackNanos != 0 && (now < callbackNanos || now - callbackNanos > lifetimeNanos); }
};
enum class Read { empty, busy, ready };

// Display-only original A, before any audition. Fixed storage, one audio writer, one UI reader.
// Every payload word is atomic: a rejected overlapping read is still data-race-free. Neither
// side retries or waits. A span change prevents a hidden/reopened page from reviving old PCM.
class LiveTap
{
public:
    static constexpr double windowSeconds = 0.080;
    static constexpr std::size_t capacity = 32'768; // 80 ms through 384 kHz
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

    void prepare (double sampleRate) noexcept
    {
        begin();
        rate = std::isfinite (sampleRate) && sampleRate > 0.0 ? sampleRate : 0.0;
        length = rate > 0.0 ? static_cast<std::size_t> (std::min (double (capacity), std::ceil (rate * windowSeconds))) : 0;
        reset(); tracking = false;
        publishedRate.store (packDouble (rate), std::memory_order_relaxed);
        publish (0);
    }
    void setEnabled (bool next) noexcept
    {
        if (wanted.load (std::memory_order_acquire) == next) return;
        demand.fetch_add (1, std::memory_order_acq_rel);
        wanted.store (next, std::memory_order_release);
    }
    bool enabled() const noexcept { return wanted.load (std::memory_order_acquire); }

    void push (const float* input, int frames, int channels, bool playing, bool hasPosition,
               std::int64_t position, std::uint64_t nanos,
               std::uint8_t clockSource = KIRIN_HYPHA_CLOCK_UNKNOWN, std::uint64_t resetGeneration = 0,
               const live_compare::BlockClock* aaxClock = nullptr) noexcept
    {
        if (! enabled() || input == nullptr || frames <= 0 || channels != 2 || length == 0)
        { interrupt(); return; }
        const auto requested = demand.load (std::memory_order_acquire);
        bool continuous = tracking && requested == seenDemand && playing == lastPlaying
                       && clockSource == lastClockSource && resetGeneration == seenReset;
        if (continuous && nanos != 0 && lastNanos != 0)
        {
            const auto elapsed = nanos >= lastNanos ? nanos - lastNanos : UINT64_MAX;
            const auto allowance = owedNanos + lastBlockNanos;
            // Burst credit may bridge scheduled callbacks, but never revive an expired trace.
            if (elapsed > traceLifetime (lastBlockNanos) || elapsed > allowance * 2 + 20'000'000ull) continuous = false;
            else owedNanos = std::min<std::uint64_t> (allowance - std::min (allowance, elapsed), 80'000'000ull);
        }
        const bool qualifiedAax = aaxClock != nullptr && live_compare::certifiedAaxClock (*aaxClock);
        std::int64_t nextEngine = 0;
        if (continuous && (qualifiedAax != lastQualifiedAax
            || (qualifiedAax && (! live_compare::sameClockProof (lastAaxClock, *aaxClock)
                || ! live_compare::checkedClockAdd (lastAaxClock.clock, lastAaxClock.frames, nextEngine)
                || aaxClock->clock != nextEngine)))) continuous = false;
        // The display has its own chunk proof. Repeated reports cannot prove a chunk until
        // its exact closing step arrives; only later callbacks receive interpolated positions.
        // Reset identity, demand and clock/gap breaks retire it, independently of trace resets.
        if (! continuous) chunkPosition.reset();
        if (qualifiedAax)
        {
            auto displayClock = *aaxClock;
            chunkPosition.apply (displayClock, rate);
            position = displayClock.project; hasPosition = displayClock.projectValid;
            if (! hasPosition) continuous = false;
        }
        // AU can supply only a render clock, without a playing transport flag. That clock still
        // advances for every accepted block, and neither a jump nor Reset may bridge old PCM.
        const bool advancing = playing || clockSource == KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE;
        if (continuous && (hasPosition != lastHasPosition
            || (hasPosition && position != (advancing ? expectedPosition : lastPosition)))) continuous = false;
        begin();
        if (! continuous) reset();
        tracking = true; seenDemand = requested; lastPlaying = playing;
        lastClockSource = clockSource; seenReset = resetGeneration;
        lastQualifiedAax = qualifiedAax;
        if (qualifiedAax) lastAaxClock = *aaxClock;
        lastHasPosition = hasPosition && position <= std::numeric_limits<std::int64_t>::max() - frames;
        lastPosition = position; expectedPosition = lastHasPosition ? position + frames : 0;
        lastNanos = nanos;
        lastBlockNanos = static_cast<std::uint64_t> (double (frames) * 1.0e9 / rate);
        // An oversized callback needs only its newest window. Its input remains entirely read-only.
        const auto first = std::max (0, frames - static_cast<int> (length));
        if (first != 0) { cursor = 0; covered = 0; }
        for (int frame = first; frame < frames; ++frame)
        {
            std::uint64_t bits;
            std::memcpy (&bits, input + static_cast<std::size_t> (frame) * 2, sizeof (bits));
            (*ring)[cursor].store (bits, std::memory_order_relaxed);
            cursor = (cursor + 1) % length;
            covered = std::min (covered + 1, length);
        }
        publish (nanos);
    }
    void interrupt() noexcept
    {
        if (! tracking) return;
        begin(); reset(); tracking = false; publish (0);
    }

    // now is the same monotonic callback clock. A silent host/bypass that ceases callbacks must
    // not leave the last trace looking live forever. Large host blocks get their actual duration.
    Read read (Snapshot& output, std::uint64_t now) const
    { return readObserved (output, now, [] (std::size_t) {}); }

private:
    // The production reader inlines the empty observer away. Native tests use the same copy
    // transaction to publish deterministically partway through a read, without timing sleeps.
    template <typename AfterSample>
    Read readObserved (Snapshot& output, std::uint64_t now, const AfterSample& afterSample) const
    {
        if (! enabled()) return Read::empty;
        const auto requested = demand.load (std::memory_order_acquire);
        const auto before = sequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0) return Read::busy;
        const auto count = publishedCount.load (std::memory_order_relaxed);
        const auto end = publishedCursor.load (std::memory_order_relaxed);
        const auto size = publishedLength.load (std::memory_order_relaxed);
        const auto stamp = publishedNanos.load (std::memory_order_relaxed);
        const auto block = publishedBlockNanos.load (std::memory_order_relaxed);
        const auto ticket = publishedDemand.load (std::memory_order_relaxed);
        const auto acceptedSpan = publishedSpan.load (std::memory_order_relaxed);
        const auto sampleRate = unpackDouble (publishedRate.load (std::memory_order_relaxed));
        if (size == 0 || count == 0 || count > size || size > capacity || ticket != requested) return Read::empty;
        output.samples.resize (count); // UI only; capacity is reused at frame rate
        const auto start = (end + size - count) % size;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto bits = (*ring)[(start + i) % size].load (std::memory_order_relaxed);
            std::memcpy (&output.samples[i], &bits, sizeof (bits));
            afterSample (i);
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        if (sequence.load (std::memory_order_relaxed) != before || demand.load (std::memory_order_acquire) != requested || ! enabled()) return Read::busy;
        // The callback may publish after the UI sampled its clock. Preserve a still-valid
        // accepted frame for this race, and judge expiry only from a coherent publication.
        if (now != 0 && stamp != 0)
        {
            if (now < stamp) return Read::busy;
            if (now - stamp > traceLifetime (block)) return Read::empty;
        }
        output.sampleRate = sampleRate; output.revision = before; output.span = acceptedSpan;
        output.callbackNanos = stamp; output.lifetimeNanos = traceLifetime (block);
        return Read::ready;
    }

    static std::uint64_t traceLifetime (std::uint64_t block) noexcept
    { return std::max<std::uint64_t> (120'000'000ull, block * 2 + 20'000'000ull); }
    static std::uint64_t packDouble (double value) noexcept
    { std::uint64_t bits; std::memcpy (&bits, &value, sizeof (bits)); return bits; }
    static double unpackDouble (std::uint64_t bits) noexcept
    { double value; std::memcpy (&value, &bits, sizeof (value)); return value; }
    void begin() noexcept
    {
        sequence.fetch_add (1, std::memory_order_relaxed);
        // Pairs with the reader's acquire fence through each atomic payload word. This also
        // prevents an overlapping reader from accepting a stale even sequence on weaker CPUs.
        std::atomic_thread_fence (std::memory_order_release);
    }
    void reset() noexcept { cursor = covered = 0; owedNanos = 0; ++epoch; }
    void publish (std::uint64_t nanos) noexcept
    {
        publishedCount.store (covered, std::memory_order_relaxed);
        publishedCursor.store (cursor, std::memory_order_relaxed);
        publishedLength.store (length, std::memory_order_relaxed);
        publishedNanos.store (nanos, std::memory_order_relaxed);
        publishedBlockNanos.store (lastBlockNanos, std::memory_order_relaxed);
        publishedDemand.store (seenDemand, std::memory_order_relaxed);
        publishedSpan.store (epoch, std::memory_order_relaxed);
        sequence.fetch_add (1, std::memory_order_release);
    }
    // Constructed before processing; the address never changes, including during prepare().
    std::unique_ptr<std::array<std::atomic<std::uint64_t>, capacity>> ring
        = std::make_unique<std::array<std::atomic<std::uint64_t>, capacity>>();
    std::atomic<bool> wanted { false };
    std::atomic<std::uint64_t> demand { 0 }, sequence { 0 };
    std::atomic<std::uint64_t> publishedCount { 0 }, publishedCursor { 0 }, publishedLength { 0 },
        publishedNanos { 0 }, publishedBlockNanos { 0 }, publishedDemand { 0 }, publishedSpan { 0 }, publishedRate { 0 };
    double rate = 0.0;
    std::size_t length = 0, cursor = 0, covered = 0;
    bool tracking = false, lastPlaying = false, lastHasPosition = false;
    std::uint8_t lastClockSource = KIRIN_HYPHA_CLOCK_UNKNOWN;
    live_compare::AaxChunkPosition chunkPosition;
    live_compare::BlockClock lastAaxClock;
    bool lastQualifiedAax = false;
    std::int64_t expectedPosition = 0, lastPosition = 0;
    std::uint64_t seenDemand = 0, seenReset = 0, epoch = 0, lastNanos = 0, lastBlockNanos = 0, owedNanos = 0;
};
static_assert (sizeof (Sample) == sizeof (std::uint64_t));
}
