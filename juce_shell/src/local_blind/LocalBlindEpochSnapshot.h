#pragma once
#include "LocalBlindTrial.h"

namespace hypha::local_blind
{
// Single non-RT admission writer, any RT reader. A torn/revoking snapshot returns no authority.
// No caller may synthesize these epochs from PID, current UI selection, or the trial's own fields.
class LocalBlindEpochSnapshot final
{
public:
    void publish (TrialEpochs value) noexcept
    {
        sequence.fetch_add (1, std::memory_order_seq_cst);
        scope.store (value.scope, std::memory_order_seq_cst);
        pair.store (value.pair, std::memory_order_seq_cst);
        capture.store (value.capture, std::memory_order_seq_cst);
        clock.store (value.clock, std::memory_order_seq_cst);
        sequence.fetch_add (1, std::memory_order_seq_cst);
    }
    TrialEpochs read() const noexcept
    {
        const auto before = sequence.load (std::memory_order_seq_cst);
        if (before & 1u) return {};
        const TrialEpochs value { scope.load (std::memory_order_seq_cst), pair.load (std::memory_order_seq_cst),
            capture.load (std::memory_order_seq_cst), clock.load (std::memory_order_seq_cst) };
        return before == sequence.load (std::memory_order_seq_cst) ? value : TrialEpochs {};
    }
private:
    std::atomic<std::uint64_t> sequence { 0 }, scope { 0 }, pair { 0 }, capture { 0 }, clock { 0 };
};
}
