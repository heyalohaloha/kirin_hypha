#pragma once

#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// END closes the user's comparison as soon as request() is accepted. This object tracks only
// the remaining audio return, not UI lifetime. The ring/renderer lease stays alive for its fade.
// An eligible audio block acknowledges actual unity; offline, bypass or another output owner never
// does. While no callback comes (the DAW stopped) nothing sounds, so after a short quiet the message
// thread finishes the return (quietSince); the first block after it rises at once (INV-LC14).
// Re-entry waits for that receipt, not another END click.
inline bool quietSince (std::uint64_t lastCallback, std::uint64_t now) noexcept
{ return lastCallback == 0 || (now >= lastCallback && now - lastCallback >= 500'000'000); }

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
    // Message thread, only after quietSince: no audio callback can hear the return.
    void complete (std::uint64_t token) noexcept
    { if (token != 0 && token == command()) completed.store (token, std::memory_order_release); }

private:
    std::atomic<std::uint64_t> requested { 0 }, completed { 0 };
};
}
