#pragma once
#include <atomic>

namespace hypha::live_compare
{
// This is output routing, not permission to skip clock preparation or MATCH invalidation.
// An absent publication is inspected without dereferencing a mapping; a concurrent new
// publication simply begins its audition on the next callback. No PRE receipt is produced.
// A fade/ramp lease, END receipt, anonymous command or held attenuation must use the full path.
inline bool unchangedPostOnly (bool publishedLease, bool finishing, bool blindActive,
                               float target, float actual) noexcept
{
    return ! publishedLease && ! finishing && ! blindActive && target == 1.0f && actual == 1.0f;
}

// This subset proves only that normal POST needs no output work. It is not a gain approval,
// identity snapshot or audible receipt. Any caller needing those reads the full tuple afresh.
template <typename State>
bool coherentUnityPostTarget (const State& state) noexcept
{
    const auto revision = state.gainRevision.load (std::memory_order_acquire);
    if ((revision & 1u) != 0) return false;
    const float target = state.postTarget.load (std::memory_order_acquire);
    std::atomic_thread_fence (std::memory_order_acquire);
    const bool coherent = revision == state.gainRevision.load (std::memory_order_acquire);
    return coherent && target == 1.0f;
}

template <typename State>
bool unchangedUnityPostOnly (const State& state, bool publishedLease, bool finishing,
                            bool blindActive, float actual) noexcept
{
    // Short-circuit before any gain read: live comparison never pays for the idle subset.
    return unchangedPostOnly (publishedLease, finishing, blindActive, 1.0f, actual)
        && coherentUnityPostTarget (state);
}
}
