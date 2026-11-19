#pragma once
#include "../src/reference_audition/ReferenceProductTestNotification.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace hypha::tests::editor_product
{
// The callback never reads product state. Its storage outlives every worker, including a
// callback already loaded when the scoped registration is removed.
class InitialSourcePhaseWait final
{
    struct Events { std::mutex mutex; std::condition_variable changed; std::uint64_t sequence = 0; };
    static Events& events() { static Events value; return value; }
public:
    InitialSourcePhaseWait()
    {
        reference_audition::ProductTestNotification expected = nullptr;
        if (!reference_audition::productTestNotification.compare_exchange_strong (expected, &notify))
            throw std::runtime_error ("initial source observer already owned");
    }
    ~InitialSourcePhaseWait() { reference_audition::productTestNotification.store (nullptr); }
    static void notify() noexcept
    {
        auto& state = events();
        { const std::lock_guard<std::mutex> lock (state.mutex); ++state.sequence; }
        state.changed.notify_all();
    }
    std::uint64_t sequence() const
    {
        auto& state = events(); const std::lock_guard<std::mutex> lock (state.mutex);
        return state.sequence;
    }
    bool waitAfter (std::uint64_t observed, std::chrono::steady_clock::time_point deadline) const
    {
        auto& state = events(); std::unique_lock<std::mutex> lock (state.mutex);
        return state.changed.wait_until (lock, deadline, [&] { return state.sequence != observed; });
    }
    InitialSourcePhaseWait (const InitialSourcePhaseWait&) = delete;
    InitialSourcePhaseWait& operator= (const InitialSourcePhaseWait&) = delete;
};
}
