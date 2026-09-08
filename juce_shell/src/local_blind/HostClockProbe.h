#pragma once
#include "../HostProcessClock.h"
#include <atomic>
#include <cstring>

namespace hypha::local_blind
{
// Atomic callback facts for diagnostics and non-RT capture planning. A snapshot is not by itself
// a PDC proof or Blind admission token; the role-local capture guard verifies later continuity.
struct HostClockProbeSnapshot
{
    std::uint64_t callback = 0;
    std::int64_t position = 0;
    double rate = 0;
    std::uint32_t frames = 0, channels = 0, inputLatency = 0, outputLatency = 0;
    std::uint8_t source = 0, presentationSource = 0;
    bool playing = false, hasPosition = false, hasInputLatency = false, hasOutputLatency = false;
};

// Keep the Audio Thread writer away from adjacent processor state when a non-RT diagnostic or
// capture action reads the snapshot. 128-byte alignment covers the wider supported host cache
// line without changing the snapshot protocol.
class alignas (128) HostClockProbe
{
public:
    // One audio producer. No allocation, lock, retry, host call or I/O.
    void publish (const HostProcessClock& clock, double sampleRate,
                  std::uint32_t blockFrames, std::uint32_t inputChannels) noexcept
    {
        sequence.fetch_add (1);
        position.store (clock.positionSamples);
        std::uint64_t bits = 0;
        std::memcpy (&bits, &sampleRate, sizeof (bits));
        rate.store (bits);
        format.store ((std::uint64_t (inputChannels) << 32u) | blockFrames);
        latency.store ((std::uint64_t (clock.outputPresentationSamples) << 32u)
                       | clock.inputPresentationSamples);
        flags.store ((std::uint64_t (clock.presentationSource) << 16u)
                     | (std::uint64_t (clock.clockSource) << 8u)
                     | (clock.playing ? 1u : 0u) | (clock.hasPosition ? 2u : 0u)
                     | (clock.inputPresentationValid ? 4u : 0u)
                     | (clock.outputPresentationValid ? 8u : 0u));
        sequence.fetch_add (1);
    }

    bool read (HostClockProbeSnapshot& out) const noexcept
    {
        out = {};
        const auto before = sequence.load();
        if (before == 0 || (before & 1u) != 0u) return false;
        HostClockProbeSnapshot next;
        next.callback = before / 2;
        next.position = position.load();
        const auto bits = rate.load();
        std::memcpy (&next.rate, &bits, sizeof (bits));
        const auto layout = format.load(), latencies = latency.load(), state = flags.load();
        next.frames = static_cast<std::uint32_t> (layout);
        next.channels = static_cast<std::uint32_t> (layout >> 32u);
        next.inputLatency = static_cast<std::uint32_t> (latencies);
        next.outputLatency = static_cast<std::uint32_t> (latencies >> 32u);
        next.playing = (state & 1u) != 0;
        next.hasPosition = (state & 2u) != 0;
        next.hasInputLatency = (state & 4u) != 0;
        next.hasOutputLatency = (state & 8u) != 0;
        next.source = static_cast<std::uint8_t> (state >> 8u);
        next.presentationSource = static_cast<std::uint8_t> (state >> 16u);
        if (before != sequence.load()) return false;
        out = next;
        return true;
    }

private:
    // Sequential consistency across every field makes the single-pass read auditable.
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert (std::atomic<std::int64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> sequence { 0 }, rate { 0 }, format { 0 }, latency { 0 }, flags { 0 };
    std::atomic<std::int64_t> position { 0 };
};
static_assert (alignof (HostClockProbe) >= 128);
}
