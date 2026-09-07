#pragma once

#include "LocalBlindTrial.h"
#include "RtPublicationSlot.h"

namespace hypha::local_blind
{
// One non-RT owner and one audio producer. This is a PCM publication/retirement slot, NOT an
// AnalysisLease or permission to audition. Never count it as a third optional Analysis owner.
class LocalBlindSlot final
{
public:
    bool publish (std::unique_ptr<LocalBlindTrial> trial) noexcept
    {
        return storage.publish (std::move (trial));
    }
    LocalBlindTrial* control() noexcept { return storage.control(); } // non-RT owner only
    bool hasPublishedRealtime() const noexcept { return storage.hasPublishedRealtime(); }

    // True means this slot owned the callback, even if an invalidated trial kept the input intact.
    // A caller must not run a second output owner (Reference) behind that result.
    bool render (float* const* data, int channels, int frames, const TrialBlock& block) noexcept
    {
        return storage.withRealtime ([&] (LocalBlindTrial& trial)
        {
            trial.render (data, channels, frames, block);
        });
    }

    bool retireAfterNormalReceipt() noexcept
    {
        auto* trial = storage.control();
        return trial != nullptr && trial->normalReturnConfirmed() && storage.retire();
    }
    bool collect() noexcept { return storage.collect(); }
    bool hasStorage() const noexcept { return storage.hasStorage(); }

private:
    RtPublicationSlot<LocalBlindTrial> storage;
};
}
