#pragma once
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// Guidance only. The callback supplies its existing timestamp; no new RT clock or syscall.
class WaitNotice
{
public:
    void begin (std::uint64_t now) noexcept
    { since.store (now, std::memory_order_release); lastBlock.store (now, std::memory_order_release); }
    void observe (bool waiting, std::uint64_t now) noexcept
    {
        lastBlock.store (now, std::memory_order_release);
        if (!waiting) since.store (0, std::memory_order_release);
        else { std::uint64_t empty = 0; since.compare_exchange_strong (empty, now, std::memory_order_acq_rel); }
    }
    bool prolonged (std::uint64_t now) const noexcept
    {
        const auto start = since.load (std::memory_order_acquire);
        const auto last = lastBlock.load (std::memory_order_acquire);
        const auto older = start != 0 ? start : last;
        return older != 0 && now >= older && now - older >= 3'000'000'000;
    }
private:
    std::atomic<std::uint64_t> since { 0 }, lastBlock { 0 };
};
}
