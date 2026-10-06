#pragma once
#include "LiveCompareRing.h"
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
struct GainIdentity
{
    std::uint64_t pair = 0, ownerA = 0, ownerB = 0, authority = 0;
    std::uint32_t rate = 0;
    bool same (const GainIdentity& other) const noexcept
    { return pair == other.pair && ownerA == other.ownerA && ownerB == other.ownerB
          && authority == other.authority && rate == other.rate; }
};
inline GainIdentity gainIdentity (const Ring& ring, std::uint64_t authority) noexcept
{
    return { ring.header.pairKey.load (std::memory_order_relaxed),
        ring.header.timing.ownerA.load (std::memory_order_acquire),
        ring.header.timing.ownerB.load (std::memory_order_acquire), authority,
        ring.header.sampleRate.load (std::memory_order_relaxed) };
}
struct GainSnapshot
{
    bool coherent = false, retained = false, limited = false;
    std::uint64_t revision = 0;
    float pre = 1.0f, post = 1.0f, ceiling = 1.0f;
    GainIdentity identity;
};
struct GainApproval
{
    std::atomic<std::uint64_t> pair { 0 }, ownerA { 0 }, ownerB { 0 }, authority { 0 };
    std::atomic<std::uint32_t> rate { 0 };
    void bind (const GainIdentity& value) noexcept // inside the gain writer lease only
    {
        pair.store (value.pair, std::memory_order_relaxed);
        ownerA.store (value.ownerA, std::memory_order_relaxed);
        ownerB.store (value.ownerB, std::memory_order_relaxed);
        authority.store (value.authority, std::memory_order_relaxed);
        rate.store (value.rate, std::memory_order_relaxed);
    }
    GainIdentity read() const noexcept // covered by the enclosing gain revision
    { return { pair.load(), ownerA.load(), ownerB.load(), authority.load(), rate.load() }; }
};
// One bounded CAS, never an RT retry or lock. Both non-RT gain publication and RT END use
// this same lease, so an END target cannot turn an in-progress MATCH revision even.
class GainUpdate
{
public:
    explicit GainUpdate (std::atomic<std::uint64_t>& ownerIn) noexcept : owner (ownerIn)
    {
        revision = owner.load (std::memory_order_acquire);
        acquired = (revision & 1u) == 0
            && owner.compare_exchange_strong (revision, revision + 1, std::memory_order_acq_rel);
        if (acquired) std::atomic_thread_fence (std::memory_order_release);
    }
    ~GainUpdate() { if (acquired) owner.store (revision + 2, std::memory_order_release); }
    explicit operator bool() const noexcept { return acquired; }
    GainUpdate (const GainUpdate&) = delete;
    GainUpdate& operator= (const GainUpdate&) = delete;
private:
    std::atomic<std::uint64_t>& owner;
    std::uint64_t revision = 0;
    bool acquired = false;
};
template <typename State>
GainSnapshot readGainSnapshot (const State& state) noexcept
{
    GainSnapshot result;
    result.revision = state.gainRevision.load (std::memory_order_acquire);
    if ((result.revision & 1u) != 0) return result;
    result.pre = state.gain.load (std::memory_order_acquire);
    result.post = state.postTarget.load (std::memory_order_acquire);
    result.ceiling = state.ceilingLinear.load (std::memory_order_acquire);
    result.limited = state.matchLimited.load (std::memory_order_acquire);
    result.retained = state.matchRetained.load (std::memory_order_acquire);
    result.identity = state.gainApproval.read();
    std::atomic_thread_fence (std::memory_order_acquire);
    result.coherent = result.revision == state.gainRevision.load (std::memory_order_acquire);
    return result;
}
template <typename State>
bool currentGainReceipt (const State& state, const GainSnapshot& gains) noexcept
{
    const auto permission = state.authority.ticket();
    const bool valid = state.authority.permitted() && gains.identity.authority == permission
        && state.sessionActive.load (std::memory_order_acquire)
        && gains.coherent && gains.retained && ! gains.limited
        && state.matched.load (std::memory_order_acquire)
        && state.matchRetained.load (std::memory_order_acquire)
        && state.gainReceipt.load (std::memory_order_acquire) == gains.revision
        && state.matchGeneration.load (std::memory_order_acquire) == state.sessionGeneration.load (std::memory_order_acquire)
        && state.matchRun.load (std::memory_order_acquire) == state.playbackRun.load (std::memory_order_acquire);
    std::atomic_thread_fence (std::memory_order_acquire);
    return valid && gains.revision == state.gainRevision.load (std::memory_order_acquire)
        && state.sessionActive.load (std::memory_order_acquire)
        && permission == state.authority.ticket() && state.authority.permitted();
}
template <typename State>
bool currentBlindReceipt (const State& state) noexcept
{
    const auto gains = readGainSnapshot (state);
    return state.blindGainRevision.load (std::memory_order_acquire) == gains.revision
        && currentGainReceipt (state, gains);
}
}
