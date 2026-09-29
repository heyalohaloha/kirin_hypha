#pragma once

#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// One message-thread writer requests END/RETURN. Only an eligible audio block acknowledges it.
// No callback, offline, bypass and another output owner never manufacture a completion.
class Completion
{
public:
    std::uint64_t request() noexcept
    {
        if (! pending()) requested.fetch_add (1, std::memory_order_acq_rel);
        return command();
    }
    std::uint64_t command() const noexcept { return requested.load (std::memory_order_acquire); }
    bool pending() const noexcept { return command() != completed.load (std::memory_order_acquire); }
    std::uint64_t receipt() const noexcept { return completed.load (std::memory_order_acquire); }

    void observe (std::uint64_t token, bool eligible, bool preAudible, float actualPostGain) noexcept
    {
        if (token != 0 && token == command() && eligible && ! preAudible && actualPostGain == 1.0f)
            completed.store (token, std::memory_order_release);
    }

private:
    std::atomic<std::uint64_t> requested { 0 }, completed { 0 };
};
}
