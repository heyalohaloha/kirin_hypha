#pragma once

#include "LocalBlindTrial.h"
#include <cassert>
#include <memory>

namespace hypha::local_blind
{
// One non-RT owner and one audio producer. This is a PCM publication/retirement slot, NOT an
// AnalysisLease or permission to audition. Never count it as a third optional Analysis owner.
class LocalBlindSlot final
{
public:
    ~LocalBlindSlot()
    {
        // Plugin lifecycle must already have quiesced the audio callback before destroying us.
        assert (readers.load() == 0);
    }

    bool publish (std::unique_ptr<LocalBlindTrial> trial) noexcept
    {
        collect();
        if (owned || retired || ! trial) return false;
        owned = std::move (trial);
        current.store (owned.get(), std::memory_order_seq_cst);
        return true;
    }
    LocalBlindTrial* control() noexcept { return owned.get(); } // single non-RT owner only
    bool hasPublishedRealtime() const noexcept { return current.load (std::memory_order_seq_cst) != nullptr; }

    // True means this slot owned the callback, even if an invalidated trial kept the input intact.
    // A caller must not run a second output owner (Reference) behind that result.
    bool render (float* const* data, int channels, int frames, const TrialBlock& block) noexcept
    {
        if (current.load (std::memory_order_seq_cst) == nullptr) return false;
        readers.fetch_add (1, std::memory_order_seq_cst);
        auto* marker = current.load (std::memory_order_seq_cst);
        if (marker != nullptr) marker->render (data, channels, frames, block);
        readers.fetch_sub (1, std::memory_order_seq_cst);
        return marker != nullptr;
    }

    bool retireAfterNormalReceipt() noexcept
    {
        if (! owned || ! owned->normalReturnConfirmed()) return false;
        current.store (nullptr, std::memory_order_seq_cst);
        retired = std::move (owned);
        collect();
        return true;
    }
    bool collect() noexcept
    {
        if (readers.load (std::memory_order_seq_cst) != 0) return false;
        retired.reset(); // only this non-RT path can destroy retired PCM
        return true;
    }
    bool hasStorage() const noexcept { return owned != nullptr || retired != nullptr; }

private:
    std::unique_ptr<LocalBlindTrial> owned, retired;
    std::atomic<LocalBlindTrial*> current { nullptr };
    std::atomic<unsigned int> readers { 0 };
    static_assert (std::atomic<LocalBlindTrial*>::is_always_lock_free);
    static_assert (std::atomic<unsigned int>::is_always_lock_free);
};
}
