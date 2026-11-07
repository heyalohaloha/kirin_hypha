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
    std::uint64_t chainA = 0, chainB = 0; // the chain and its PDC values
    std::uint32_t delay = 0;
    bool valid = false;
    std::uint64_t layoutA = 0, layoutB = 0, reported = 0; // the same chain without them; the sum of its FX reports
    bool same (const ReaperPdcObservation& b) const noexcept
    { return valid && b.valid && chainA == b.chainA && chainB == b.chainB && delay == b.delay; }
    bool sameLayout (const ReaperPdcObservation& b) const noexcept
    { return valid && b.valid && layoutA == b.layoutA && layoutB == b.layoutB; }
    // REAPER's chain values are live: every FX reads 0 just after a seek while the delay buffer is
    // rebuilt, and one short callback at a loop wrap lowers the fill for as long as it stays inside.
    // Both only ever fall below the settled values of the same chain.
    bool refilling (const ReaperPdcObservation& settled) const noexcept
    {
        return sameLayout (settled) && delay <= settled.delay && reported <= settled.reported
            && (delay < settled.delay || reported < settled.reported);
    }
};

class ReaperPdcMailbox
{
public:
    // Fixed before host acceptance: timer normally polls at 50 ms; a stalled timer loses
    // permission after 500 ms or two seconds of processed samples (including preroll bursts).
    // Neither expiry converts wall time into a PCM position or proves host API freshness.
    static constexpr std::uint64_t lifetimeNanos = 500'000'000;
    // A lower reading of the same chain stays the old proof for this long: the bound stays the
    // settled, larger one and every address is still checked. Longer than a rebuild, so a latency
    // the user really lowered still arrives as a new proof, at most this much later.
    static constexpr std::uint64_t refillNanos = 1'500'000'000;
    void invalidate() noexcept { epoch.fetch_add (1, std::memory_order_acq_rel); }
    void observe (BlockClock& b, double rate, bool usable) noexcept
    {
        b.runtimeChainA = b.runtimeChainB = b.runtimeRevision = 0;
        b.maximumDelaySamples = 0;
        b.runtimeExpired = false;
        const auto currentEpoch = epoch.load (std::memory_order_acquire);
        auto observed = b;
        observed.clockValid = observed.clockValid && usable;
        const auto movement = timeline.observe (observed, rate);
        if (movement.broken || currentEpoch != seenEpoch || (!wasPlaying && b.playing)) ++run;
        seenEpoch = currentEpoch; wasPlaying = b.playing && usable;
        ++serial;
        if (b.frames > 0) samples += static_cast<std::uint64_t> (b.frames);
        callbacks.store ({ currentEpoch, run, serial, samples, b.wallNanos, wasPlaying ? 1u : 0u });
        if (!usable || !b.playing) return;
        std::array<std::uint64_t, 10> v {};
        const bool read = token.load (v);
        const bool current = read && v[0] == currentEpoch && v[1] == run && v[5] != 0 && v[6] != 0;
        // A publication landing inside this callback is newer than the block. That is a race,
        // not a revocation: the block keeps the token its predecessor used, as it would have
        // if the timer had fired a moment later. An empty or older-run token does revoke it.
        const bool raced = !read || (current && (serial <= v[2] || samples < v[3] || b.wallNanos < v[4]));
        if (current && !raced) held = v;
        else if (!raced) held = {};
        if (held[0] != currentEpoch || held[1] != run || held[5] == 0 || held[6] == 0) return;
        b.runtimeChainA = held[5]; b.runtimeChainB = held[6]; b.runtimeRevision = held[7];
        b.maximumDelaySamples = static_cast<std::uint32_t> (held[8]);
        b.runtimeExpired = samples - held[3] > rate * 2.0 || b.wallNanos - held[4] > lifetimeNanos;
    }
    bool witness (ReaperCallbackWitness& out) const noexcept
    {
        std::array<std::uint64_t, 6> v {};
        if (!callbacks.load (v) || v[0] != epoch.load (std::memory_order_acquire)) return false;
        out = { v[0], v[1], v[2], v[3], v[4], v[5] != 0 }; return true;
    }
    // Message thread, after host reads bracketed by callback witnesses. A true API flag
    // before playback is discarded, even if its number already equals the future latency.
    void publish (const ReaperPdcObservation& observed, const ReaperCallbackWitness& before,
                  const ReaperCallbackWitness& after, bool hostPlaying, std::uint64_t now) noexcept
    {
        const bool bracketed = observed.valid && hostPlaying && before.playing && after.playing
            && before.epoch == after.epoch && before.run == after.run
            && before.epoch == epoch.load (std::memory_order_acquire)
            && after.serial >= before.serial && now >= after.nanos
            && now - after.nanos <= lifetimeNanos;
        auto reading = observed;
        if (bracketed && provenEpoch == before.epoch && observed.refilling (proven))
        {
            if (! observed.same (lowered)) { lowered = observed; loweredSince = now; }
            if (now - loweredSince < refillNanos) reading = proven;
        }
        else lowered = {};
        const bool stable = bracketed && reading.same (previous) && prior.epoch == before.epoch
            && prior.run == before.run && after.serial > prior.serial;
        // A large callback can span two timer ticks. Keep its existing expiry, rather than
        // revoking between normal callbacks or extending a token without any audio progress.
        if (bracketed && reading.same (previous) && prior.epoch == before.epoch
            && prior.run == before.run && after.serial == prior.serial) return;
        // The revision names the chain and delay, not one observation of them: a stop or seek
        // re-reads the same proof, and only a different reading or a host epoch changes it.
        if (prior.epoch != before.epoch || (bracketed && !reading.same (proven))) ++revision;
        if (bracketed) { proven = reading; provenEpoch = before.epoch; }
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
    std::array<std::uint64_t, 10> held {};
    bool wasPlaying = false;
    ReaperPdcObservation previous, proven, lowered;
    ReaperCallbackWitness prior;
    std::uint64_t provenEpoch = 0, loweredSince = 0;
    std::uint64_t revision = 1;
};
}
