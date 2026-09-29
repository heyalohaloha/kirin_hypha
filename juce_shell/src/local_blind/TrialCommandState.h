#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace hypha::local_blind
{
// One non-RT control owner may replace its intent. The audio callback may only advance the
// exact command it rendered; automatic playback must never overwrite a later STOP/RETURN.
class TrialCommandState final
{
public:
    std::uint64_t load (std::memory_order order = std::memory_order_acquire) const noexcept
    { return value.load (order); }

    bool issue (std::uint64_t kind) noexcept // non-RT owner only
    {
        auto current = load (std::memory_order_relaxed);
        do
        {
            if (! canAdvance (current, kind)) return false;
        }
        while (! value.compare_exchange_weak (current, next (current, kind),
                                              std::memory_order_release, std::memory_order_relaxed));
        return true;
    }

    bool advanceRendered (std::uint64_t rendered, std::uint64_t kind) noexcept // RT: one attempt
    {
        return canAdvance (rendered, kind)
            && value.compare_exchange_strong (rendered, next (rendered, kind),
                                               std::memory_order_release, std::memory_order_relaxed);
    }

private:
    static bool canAdvance (std::uint64_t current, std::uint64_t kind) noexcept
    { return kind < 8 && current <= std::numeric_limits<std::uint64_t>::max() - 16; }
    static std::uint64_t next (std::uint64_t current, std::uint64_t kind) noexcept
    { return ((current >> 3) + 1) * 8 + kind; }
    std::atomic<std::uint64_t> value { 0 };
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
};
}
