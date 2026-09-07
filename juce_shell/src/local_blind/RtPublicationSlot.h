#pragma once

#include <atomic>
#include <cassert>
#include <memory>
#include <utility>

namespace hypha::local_blind
{
// One non-RT owner publishes one preallocated payload to one Audio Thread reader. The owner
// removes the realtime pointer before retirement and releases storage only after all readers exit.
template <typename Payload>
class RtPublicationSlot final
{
public:
    ~RtPublicationSlot()
    {
        // The host must quiesce the audio callback before destroying the containing processor.
        assert (readers.load (std::memory_order_seq_cst) == 0);
    }

    bool publish (std::unique_ptr<Payload> payload) noexcept
    {
        collect();
        if (owned || retired || ! payload)
            return false;
        owned = std::move (payload);
        current.store (owned.get(), std::memory_order_seq_cst);
        return true;
    }

    template <typename Operation>
    bool withRealtime (Operation&& operation) noexcept
    {
        if (current.load (std::memory_order_seq_cst) == nullptr)
            return false;
        readers.fetch_add (1, std::memory_order_seq_cst);
        auto* payload = current.load (std::memory_order_seq_cst);
        if (payload != nullptr)
            std::forward<Operation> (operation) (*payload);
        readers.fetch_sub (1, std::memory_order_seq_cst);
        return payload != nullptr;
    }

    Payload* control() noexcept { return owned.get(); }
    const Payload* control() const noexcept { return owned.get(); }
    bool hasPublishedRealtime() const noexcept
    {
        return current.load (std::memory_order_seq_cst) != nullptr;
    }

    bool retire() noexcept
    {
        if (! owned)
            return false;
        current.store (nullptr, std::memory_order_seq_cst);
        retired = std::move (owned);
        collect();
        return true;
    }

    bool collect() noexcept
    {
        if (readers.load (std::memory_order_seq_cst) != 0)
            return false;
        retired.reset();
        return true;
    }

    bool hasStorage() const noexcept { return owned != nullptr || retired != nullptr; }

private:
    std::unique_ptr<Payload> owned, retired;
    std::atomic<Payload*> current { nullptr };
    std::atomic<unsigned int> readers { 0 };
    static_assert (std::atomic<Payload*>::is_always_lock_free);
    static_assert (std::atomic<unsigned int>::is_always_lock_free);
};
}
