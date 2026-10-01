#pragma once

#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// END closes the user's comparison as soon as request() is accepted. This object tracks only
// the remaining audio return, not UI lifetime. The ring/renderer lease stays alive for its fade.
// Only an eligible audio block acknowledges actual unity; no callback, offline, bypass or other
// output owner manufactures a receipt. Re-entry waits for that receipt, not another END click.
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
