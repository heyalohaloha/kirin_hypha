#pragma once
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// A hold belongs to explicit stop evidence, not to a callback timeout or a seek. A delayed
// message-thread estimate cannot reinstate a hold after the host has already stopped.
class ContentHold
{
public:
    std::uint64_t generation() const noexcept { return stops.load (std::memory_order_acquire); }
    void hold (std::uint64_t measuredGeneration) noexcept
    { heldAt.store (measuredGeneration, std::memory_order_release); }
    void stopped() noexcept { stops.fetch_add (1, std::memory_order_acq_rel); }
    bool held() const noexcept
    {
        const auto ticket = heldAt.load (std::memory_order_acquire);
        return ticket != 0 && ticket == generation();
    }
private:
    std::atomic<std::uint64_t> stops { 1 }, heldAt { 0 };
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "Content hold must be lock-free");
}
