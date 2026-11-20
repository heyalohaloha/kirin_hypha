#pragma once
#if ! defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
 #error Reference product notifications belong only to the test-clock processor
#endif
#include <atomic>

namespace hypha::reference_audition
{
using ProductTestNotification = void (*)() noexcept;
inline std::atomic<ProductTestNotification> productTestNotification { nullptr };
inline void notifyReferenceProductTest() noexcept
{
    if (const auto notify = productTestNotification.load (std::memory_order_acquire)) notify();
}
}
