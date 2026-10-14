#pragma once
#include <chrono>
#include <cstdint>
#if defined (KIRIN_HYPHA_TEST_WALL_CLOCK)
 #include <atomic>
#endif

namespace hypha::live_compare
{
#if defined (KIRIN_HYPHA_TEST_WALL_CLOCK)
// Defined only in the separately compiled native-test library. Zero opts into the real clock
// for elapsed-time benchmarks. A running synthetic host publishes a nonzero monotonic value.
inline std::atomic<std::uint64_t> testCallbackNanos { 0 };
static_assert (decltype (testCallbackNanos)::is_always_lock_free);
#endif

inline std::uint64_t callbackWallNanos() noexcept
{
   #if defined (KIRIN_HYPHA_TEST_WALL_CLOCK)
    const auto testNanos = testCallbackNanos.load (std::memory_order_relaxed);
    if (testNanos != 0) return testNanos;
   #endif
    return static_cast<std::uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}
