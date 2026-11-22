#pragma once
#include "LiveCompareGainApproval.h"

namespace hypha::live_compare
{
#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
// 0: the plan passed every check and the commit lock is not taken yet.
// 1: the last check passed under the commit lock and the gains are about to be published.
inline void (*duringLiveMatchCommitForTest) (int stage) = nullptr;
#endif
inline void liveMatchCommitStage ([[maybe_unused]] int stage)
{
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (duringLiveMatchCommitForTest) duringLiveMatchCommitForTest (stage);
   #endif
}

// The PRE clock and run an ALL PLAYED approval was measured on, bound to the one gain revision that
// approval published, beside the tuple it replaced. PRE is another audio thread: it can move its
// clock after the last non-RT check, and no lock of POST's stops it. POST's audio thread therefore
// confirms the proof on the first block that reads this revision. An expired proof puts the replaced
// tuple back with one bounded CAS, before any output used the approval: no wait and no retry.
struct GainProof
{
    // Written inside the approving gain lease, read by the audio thread on an unchanged revision.
    std::atomic<std::uint64_t> revision { 0 }, clock { 0 }, run { 0 };
    GainApproval identity;
    std::atomic<float> pre { 1.0f }, post { 1.0f }, ceiling { 1.0f };
    std::atomic<bool> limited { false }, retained { false };
    std::atomic<std::uint64_t> discarded { 0 }; // the approval the audio thread put back
    std::uint64_t seen = 0;                     // Audio Thread only: the last approval it decided
    double peakBefore = 0.0;                    // message thread: the PRE peak that approval replaced
    std::uint64_t peakSettled = 0;

    // Message thread. A discarded approval also returns the PRE true peak MATCH on checks against.
    void settlePeak (double& peak) noexcept
    {
        const auto refused = discarded.load (std::memory_order_acquire);
        if (refused == 0 || refused != revision.load (std::memory_order_relaxed) || refused == peakSettled) return;
        peak = peakBefore;
        peakSettled = refused;
    }

    // Message thread, inside the gain lease and before the new tuple is stored. An approval without
    // an ALL PLAYED proof (LAST 4 s) binds nothing: the audio thread keeps it as before.
    template <typename State>
    void keep (State& state, std::uint64_t published, bool bound, std::uint64_t preClock, std::uint64_t preRun) noexcept
    {
        settlePeak (state.matchPeakDbtp);
        revision.store (bound ? published : 0, std::memory_order_relaxed);
        if (! bound) return;
        clock.store (preClock, std::memory_order_relaxed);
        run.store (preRun, std::memory_order_relaxed);
        identity.bind (state.gainApproval.read());
        pre.store (state.gain.load (std::memory_order_relaxed), std::memory_order_relaxed);
        post.store (state.postTarget.load (std::memory_order_relaxed), std::memory_order_relaxed);
        ceiling.store (state.ceilingLinear.load (std::memory_order_relaxed), std::memory_order_relaxed);
        limited.store (state.matchLimited.load (std::memory_order_relaxed), std::memory_order_relaxed);
        retained.store (state.matchRetained.load (std::memory_order_relaxed), std::memory_order_relaxed);
        peakBefore = state.matchPeakDbtp;
    }
};

// Audio Thread, once per block on the snapshot it just read. One load while nothing new is bound.
// The flags outside the tuple are only ever cleared here: a replaced MATCH stays held until a new
// proof matches it again, and a stop that landed meanwhile is never undone.
template <typename State>
void confirmGainProof (State& state, GainSnapshot& gains) noexcept
{
    auto& proof = state.gainProof;
    const auto bound = proof.revision.load (std::memory_order_acquire);
    if (bound == proof.seen || ! gains.coherent) return;
    const auto clock = proof.clock.load (std::memory_order_relaxed);
    const auto run = proof.run.load (std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_acquire);
    if (state.gainRevision.load (std::memory_order_acquire) != gains.revision)
    {
        gains.coherent = false; // a writer began: this block keeps the last whole tuple
        return;
    }
    proof.seen = bound;
    if (bound != gains.revision) return; // nothing bound, or a later writer already replaced that tuple
    bool current = false;
    state.ring.withRealtime ([&] (const auto& mapping)
    {
        const auto* ring = mapping.ring();
        current = ring != nullptr && ring->header.ownerClosed.load (std::memory_order_acquire) == 0
            && ring->header.run.load (std::memory_order_acquire) == run
            && ring->header.timing.generation.load (std::memory_order_acquire) == clock;
    });
    if (current) return;
    {
        GainUpdate update (state.gainRevision, gains.revision);
        if (update)
        {
            state.gainApproval.bind (proof.identity.read());
            state.ceilingLinear.store (proof.ceiling.load (std::memory_order_relaxed), std::memory_order_release);
            state.gain.store (proof.pre.load (std::memory_order_relaxed), std::memory_order_release);
            state.postTarget.store (proof.post.load (std::memory_order_relaxed), std::memory_order_release);
            state.matchLimited.store (proof.limited.load (std::memory_order_relaxed), std::memory_order_release);
            if (! proof.retained.load (std::memory_order_relaxed))
                state.matchRetained.store (false, std::memory_order_release);
            state.matched.store (false, std::memory_order_release);
            proof.discarded.store (bound, std::memory_order_release);
        }
    }
    gains = readGainSnapshot (state); // the tuple put back, or the writer that landed first
}
}
