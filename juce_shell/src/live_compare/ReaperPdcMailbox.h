#pragma once
#include "ReaperTiming.h"
#include <array>

namespace hypha::live_compare
{
// Single writer per mailbox. Payload words are atomic too: a failed sequence check never
// excuses a C++ data race. RT reads once, without a spin, host call, string or allocation.
template <std::size_t N> class AtomicWords
{
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
public:
    void store (const std::array<std::uint64_t, N>& v) noexcept
    {
        const auto seq = sequence.load (std::memory_order_relaxed);
        sequence.store (seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        for (std::size_t i = 0; i < N; ++i) words[i].store (v[i], std::memory_order_relaxed);
        sequence.store (seq + 2, std::memory_order_release);
    }
    bool load (std::array<std::uint64_t, N>& v) const noexcept
    {
        const auto seq = sequence.load (std::memory_order_acquire);
        if (seq == 0 || (seq & 1u) != 0) return false;
        for (std::size_t i = 0; i < N; ++i) v[i] = words[i].load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        return seq == sequence.load (std::memory_order_relaxed);
    }
private:
    std::atomic<std::uint64_t> sequence { 0 };
    std::array<std::atomic<std::uint64_t>, N> words {};
};

struct ReaperCallbackWitness
{
    std::uint64_t epoch = 0, run = 0, serial = 0, samples = 0, nanos = 0;
    bool playing = false;
};
struct ReaperPdcObservation
{
    std::uint64_t chainA = 0, chainB = 0;
    std::uint32_t delay = 0;
    bool valid = false;
    bool same (const ReaperPdcObservation& b) const noexcept
    { return valid && b.valid && chainA == b.chainA && chainB == b.chainB && delay == b.delay; }
};

class ReaperPdcMailbox
{
public:
    // Fixed before host acceptance: timer normally polls at 50 ms; a stalled timer loses
    // permission after 500 ms or two seconds of processed samples (including preroll bursts).
    // Neither expiry converts wall time into a PCM position or proves host API freshness.
    static constexpr std::uint64_t lifetimeNanos = 500'000'000;
    void invalidate() noexcept { epoch.fetch_add (1, std::memory_order_acq_rel); }
    void observe (BlockClock& b, double rate, bool usable) noexcept
    {
        b.runtimeChainA = b.runtimeChainB = b.runtimeRevision = 0;
        b.maximumDelaySamples = 0;
        const auto currentEpoch = epoch.load (std::memory_order_acquire);
        auto observed = b;
        observed.clockValid = observed.clockValid && usable;
        const auto movement = timeline.observe (observed, rate);
        if (movement.broken || currentEpoch != seenEpoch || (!wasPlaying && b.playing)) ++run;
        seenEpoch = currentEpoch; wasPlaying = b.playing && usable;
        ++serial;
        if (b.frames > 0) samples += static_cast<std::uint64_t> (b.frames);
        callbacks.store ({ currentEpoch, run, serial, samples, b.wallNanos, wasPlaying ? 1u : 0u });
        std::array<std::uint64_t, 10> v {};
        if (!usable || !b.playing || !token.load (v) || v[0] != currentEpoch || v[1] != run
            || serial <= v[2] || samples < v[3] || samples - v[3] > rate * 2.0
            || b.wallNanos < v[4] || b.wallNanos - v[4] > lifetimeNanos || v[5] == 0 || v[6] == 0)
            return;
        b.runtimeChainA = v[5]; b.runtimeChainB = v[6]; b.runtimeRevision = v[7];
        b.maximumDelaySamples = static_cast<std::uint32_t> (v[8]);
    }
    bool witness (ReaperCallbackWitness& out) const noexcept
    {
        std::array<std::uint64_t, 6> v {};
        if (!callbacks.load (v) || v[0] != epoch.load (std::memory_order_acquire)) return false;
        out = { v[0], v[1], v[2], v[3], v[4], v[5] != 0 }; return true;
    }
    // Message thread, after host reads bracketed by callback witnesses. A true API flag
    // before playback is discarded, even if its number already equals the future latency.
    void publish (const ReaperPdcObservation& reading, const ReaperCallbackWitness& before,
                  const ReaperCallbackWitness& after, bool hostPlaying, std::uint64_t now) noexcept
    {
        const bool bracketed = reading.valid && hostPlaying && before.playing && after.playing
            && before.epoch == after.epoch && before.run == after.run
            && before.epoch == epoch.load (std::memory_order_acquire)
            && after.serial >= before.serial && now >= after.nanos
            && now - after.nanos <= lifetimeNanos;
        const bool stable = bracketed && reading.same (previous) && prior.epoch == before.epoch
            && prior.run == before.run && after.serial > prior.serial;
        // A large callback can span two timer ticks. Keep its existing expiry, rather than
        // revoking between normal callbacks or extending a token without any audio progress.
        if (bracketed && reading.same (previous) && prior.epoch == before.epoch
            && prior.run == before.run && after.serial == prior.serial) return;
        if (!bracketed || !reading.same (previous) || prior.epoch != before.epoch || prior.run != before.run)
            ++revision;
        token.store (stable ? std::array<std::uint64_t, 10> { after.epoch, after.run, after.serial,
            after.samples, now, reading.chainA, reading.chainB, revision, reading.delay, 1 } : std::array<std::uint64_t, 10> {});
        previous = bracketed ? reading : ReaperPdcObservation {}; prior = after;
    }
private:
    std::atomic<std::uint64_t> epoch { 1 };
    AtomicWords<6> callbacks;
    AtomicWords<10> token;
    // Below: RT-owned continuity, followed by message-thread-owned publication history.
    LoopTimeline timeline;
    std::uint64_t run = 0, serial = 0, samples = 0, seenEpoch = 0;
    bool wasPlaying = false;
    ReaperPdcObservation previous;
    ReaperCallbackWitness prior;
    std::uint64_t revision = 1;
};
}
