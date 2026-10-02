#pragma once

#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
enum class StartResult : std::uint8_t
{
    started, notPost, notReady, noPair, unsupportedLayout, preUnavailable, preMultiMono,
    comparisonBusy, returnRequired, returnPending
};

// A host restore may run off the message thread. Revoke output before reading the state,
// without touching the renderer, its mapping, the POST level or an outstanding END receipt.
// The low word counts overlapping restores; the high word never reuses an old permission.
class Authority
{
public:
    class RestoreScope
    {
    public:
        explicit RestoreScope (Authority& ownerIn) noexcept : owner (ownerIn)
        { owner.state.fetch_add (epochStep + 1, std::memory_order_acq_rel); }
        ~RestoreScope() { owner.state.fetch_sub (1, std::memory_order_release); }
        RestoreScope (const RestoreScope&) = delete;
        RestoreScope& operator= (const RestoreScope&) = delete;
    private:
        Authority& owner;
    };

    RestoreScope restoringState() noexcept { return RestoreScope (*this); }
    std::uint64_t ticket() const noexcept { return state.load (std::memory_order_acquire); }
    std::uint64_t generation() const noexcept { return ticket() >> 32; }
    bool restoring() const noexcept { return (ticket() & depthMask) != 0; }
    // Explicit pair mutation invalidates the old permission before any FFI/state mutation.
    std::uint64_t revoke() noexcept
    { return (state.fetch_add (epochStep, std::memory_order_acq_rel) + epochStep) >> 32; }
    bool permitted() const noexcept
    {
        const auto current = ticket();
        return (current & depthMask) == 0 && current == permission.load (std::memory_order_acquire);
    }
    // Message thread, explicit new session only. A slow start cannot arm across a restore.
    bool arm (std::uint64_t requested) noexcept
    {
        if ((requested & depthMask) != 0 || ticket() != requested) return false;
        permission.store (requested, std::memory_order_release);
        return permitted();
    }
private:
    static constexpr std::uint64_t epochStep = std::uint64_t (1) << 32;
    static constexpr std::uint64_t depthMask = epochStep - 1;
    std::atomic<std::uint64_t> state { 0 }, permission { 0 };
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "Restore authority must be RT safe");

// One admission rule for the processor and every UI entry. Only a still-active session can
// carry its own approved attenuation into Blind; neither a target nor a completed UI action
// substitutes for actual unity. A new session never implicitly raises POST.
inline StartResult entryAdmission (bool reuseSession, bool active, bool restoring, bool finishing,
                                   bool blindOwned, float actual, float target) noexcept
{
    if (restoring) return StartResult::notReady;
    if (finishing) return StartResult::returnPending;
    if ((! reuseSession || ! active) && (actual != 1.0f || target != 1.0f))
        return StartResult::returnRequired;
    if (blindOwned) return StartResult::comparisonBusy;
    return StartResult::started;
}
}
