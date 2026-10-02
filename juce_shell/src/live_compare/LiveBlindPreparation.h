#pragma once
#include "LiveCompareRecovery.h"

namespace hypha::live_compare
{
// One preparation epoch owns both its first established timing and its terminal reason.
// Startup waits are not failures. After timing was established, any real discontinuity ends
// this preparation; fresh clocks cannot silently turn it into a new anonymous trial.
class BlindPreparation
{
public:
    struct Command { std::uint64_t word = 0, requiredAdmission = 0; };
    void begin (std::uint64_t requiredAdmission) noexcept // message thread only
    {
        const auto epoch = static_cast<std::uint64_t> (++serial) << 32;
        state.store (epoch, std::memory_order_release); // close before changing the epoch payload
        std::atomic_thread_fence (std::memory_order_release);
        admission.store (requiredAdmission, std::memory_order_relaxed);
        state.store (epoch | 1u, std::memory_order_release); // never inherit an old proof
    }
    void end() noexcept { state.fetch_and (~std::uint64_t (1), std::memory_order_acq_rel); }
    Command command() const noexcept
    {
        const auto word = state.load (std::memory_order_acquire);
        const auto request = admission.load (std::memory_order_acquire);
        std::atomic_thread_fence (std::memory_order_acquire);
        return word == state.load (std::memory_order_acquire) ? Command { word, request } : Command {};
    }
    void observe (Command command, bool timingProven, RecoveryReason loss,
                  std::uint64_t admissionReceipt) noexcept
    {
        if ((command.word & 1u) == 0 || (command.word & 4u) != 0
            || command.requiredAdmission != admissionReceipt) return;
        auto next = command.word;
        if ((command.word & 2u) != 0 && loss != RecoveryReason::none)
            next |= 4u | (static_cast<std::uint64_t> (loss) << 8);
        else if (timingProven && loss == RecoveryReason::none) next |= 2u;
        if (next != command.word) state.compare_exchange_strong (command.word, next, std::memory_order_acq_rel);
    }
    RecoveryReason failure() const noexcept
    {
        const auto value = state.load (std::memory_order_acquire);
        return (value & 4u) != 0 ? static_cast<RecoveryReason> ((value >> 8) & 0xffu) : RecoveryReason::none;
    }
    bool failedAndOpen() const noexcept
    { return (state.load (std::memory_order_acquire) & 5u) == 5u; }
private:
    std::uint32_t serial = 0;
    std::atomic<std::uint64_t> state { 0 }, admission { 0 };
};
}
