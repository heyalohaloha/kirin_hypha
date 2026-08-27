#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace hypha::pre_display
{
    enum class ClockSource : std::uint8_t
    {
        unknown = 0,
        projectTimeline = 1,
        audioRenderTimeline = 2,
    };

    // A render timeline is exact for audio processing, but it is not guaranteed to share
    // the DAW project's zero point. Project-time guides must never be projected from it.
    constexpr bool canProjectGuideTime (ClockSource source) noexcept
    {
        return source == ClockSource::projectTimeline;
    }

    struct ClockSnapshot
    {
        std::uint64_t generation = 0;
        std::int64_t positionSamples = 0;
        double sampleRate = 0.0;
        std::uint32_t blockFrames = 0;
        bool playing = false;
        ClockSource source = ClockSource::unknown;
    };

    class ClockTap final
    {
    public:
        void publish (std::int64_t positionSamples,
                      double sampleRate,
                      std::uint32_t blockFrames,
                      bool playing,
                      ClockSource source) noexcept
        {
            sequence.fetch_add (1, std::memory_order_acq_rel);
            position.store (positionSamples, std::memory_order_relaxed);
            sampleRateBits.store (doubleBits (sampleRate), std::memory_order_relaxed);
            frames.store (blockFrames, std::memory_order_relaxed);
            isPlaying.store (playing, std::memory_order_relaxed);
            sourceCode.store (static_cast<std::uint8_t> (source), std::memory_order_relaxed);
            generation.fetch_add (1, std::memory_order_relaxed);
            sequence.fetch_add (1, std::memory_order_release);
        }

        bool read (ClockSnapshot& out) const noexcept
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const auto before = sequence.load (std::memory_order_acquire);
                if ((before & 1u) != 0u)
                    continue;
                ClockSnapshot candidate;
                candidate.generation = generation.load (std::memory_order_relaxed);
                candidate.positionSamples = position.load (std::memory_order_relaxed);
                candidate.sampleRate = bitsDouble (sampleRateBits.load (std::memory_order_relaxed));
                candidate.blockFrames = frames.load (std::memory_order_relaxed);
                candidate.playing = isPlaying.load (std::memory_order_relaxed);
                candidate.source = static_cast<ClockSource> (sourceCode.load (std::memory_order_relaxed));
                const auto after = sequence.load (std::memory_order_acquire);
                if (before == after && (after & 1u) == 0u)
                {
                    out = candidate;
                    return candidate.generation != 0;
                }
            }
            return false;
        }

    private:
        static std::uint64_t doubleBits (double value) noexcept
        {
            std::uint64_t bits = 0;
            std::memcpy (&bits, &value, sizeof (bits));
            return bits;
        }

        static double bitsDouble (std::uint64_t bits) noexcept
        {
            double value = 0.0;
            std::memcpy (&value, &bits, sizeof (value));
            return value;
        }

        static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                       "PRE display clock must remain lock-free on shipped 64-bit targets");
        static_assert (std::atomic<std::int64_t>::is_always_lock_free,
                       "PRE display position must remain lock-free on shipped 64-bit targets");

        std::atomic<std::uint64_t> sequence { 0 };
        std::atomic<std::uint64_t> generation { 0 };
        std::atomic<std::int64_t> position { 0 };
        std::atomic<std::uint64_t> sampleRateBits { 0 };
        std::atomic<std::uint32_t> frames { 0 };
        std::atomic<bool> isPlaying { false };
        std::atomic<std::uint8_t> sourceCode { 0 };
    };

    // The seqlock reader may legitimately miss a poll while the audio thread is
    // publishing. Keep the last coherent snapshot; a transient miss must never
    // make a guide disappear or jump to an unknown clock.
    class ClockReaderState final
    {
    public:
        bool refresh (const ClockTap& tap, std::int64_t nowMs) noexcept
        {
            ClockSnapshot candidate;
            if (! tap.read (candidate))
                return false;
            accept (candidate, nowMs);
            return true;
        }

        void accept (const ClockSnapshot& candidate, std::int64_t nowMs) noexcept
        {
            if (! hasSnapshot || candidate.generation != retained.generation)
                retainedObservedAtMs = nowMs;
            retained = candidate;
            hasSnapshot = true;
        }

        const ClockSnapshot& snapshot() const noexcept { return retained; }
        std::int64_t observedAtMs() const noexcept { return retainedObservedAtMs; }
        bool hasValue() const noexcept { return hasSnapshot; }

    private:
        ClockSnapshot retained;
        std::int64_t retainedObservedAtMs = 0;
        bool hasSnapshot = false;
    };
}
