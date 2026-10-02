#pragma once

#include "LiveCompareCorrespondence.h"
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// Facts observed at the safety boundary, not guesses about a plug-in or a user's action.
enum class RecoveryReason : std::uint8_t
{
    none, stopped, positionChanged, callbackGap, projectClockMissing, clockMissing,
    calibrating, writing, beforeRun, notWritten, overwritten, torn, foreignRing,
    pairChanged, preUnavailable, formatChanged, restored, compensationOff, contentChanged,
    bypassed, offline, outputTaken, gainChanged, nonFinite, ceiling, blockTooLarge,
    randomUnavailable, unknown, loopUnproven, loopWaiting, loopTooShort, loopClockUnavailable
};

inline RecoveryReason recoveryReason (Verdict verdict) noexcept
{
    using R = RecoveryReason;
    switch (verdict)
    {
        case Verdict::accepted: return R::none;
        case Verdict::foreignRing: return R::foreignRing;
        case Verdict::noClock: return R::clockMissing;
        case Verdict::stopped: return R::stopped;
        case Verdict::calibrating: return R::calibrating;
        case Verdict::writing: return R::writing;
        case Verdict::beforeRun: return R::beforeRun;
        case Verdict::notWritten: return R::notWritten;
        case Verdict::overwritten: return R::overwritten;
        case Verdict::torn: return R::torn;
        case Verdict::loopUnproven: return R::loopUnproven;
        case Verdict::loopWaiting: return R::loopWaiting;
    }
    return R::unknown;
}

inline RecoveryReason recoveryReason (LoopEntryFailure failure) noexcept
{
    switch (failure)
    {
        case LoopEntryFailure::none: return RecoveryReason::loopUnproven;
        case LoopEntryFailure::observingCycle: return RecoveryReason::loopWaiting;
        case LoopEntryFailure::clockUnavailable: return RecoveryReason::loopClockUnavailable;
        case LoopEntryFailure::loopTooShort: return RecoveryReason::loopTooShort;
    }
    return RecoveryReason::loopUnproven;
}

struct SelectionCommand
{
    std::uint64_t word = 0;
    bool pre() const noexcept { return (word & 3u) == 1u; }
    RecoveryReason reason() const noexcept { return static_cast<RecoveryReason> ((word >> 8) & 0xffu); }
};

// The selection and its terminal reason share one atomic word. A callback can close only the
// exact command it sampled, never clear a newer PRE selection. END seals without inventing a
// failure; only the next explicit selection clears the retained reason.
class NamedSelection
{
public:
    SelectionCommand command() const noexcept { return { value.load (std::memory_order_acquire) }; }
    void select (bool pre) noexcept // message thread only
    { value.store ((static_cast<std::uint64_t> (++serial) << 32) | (pre ? 1u : 0u), std::memory_order_release); }
    void fail (SelectionCommand cmd, RecoveryReason cause) noexcept
    {
        if (cause == RecoveryReason::none || (cmd.word & 2u) != 0) return;
        const auto terminal = (cmd.word & ~std::uint64_t (1)) | 2u
            | (static_cast<std::uint64_t> (cause) << 8);
        value.compare_exchange_strong (cmd.word, terminal, std::memory_order_acq_rel);
    }
    void end (RecoveryReason cause = RecoveryReason::none) noexcept
    {
        auto cmd = command();
        for (int attempt = 0; attempt < 2 && (cmd.word & 2u) == 0; ++attempt)
        {
            const auto terminal = (cmd.word & ~std::uint64_t (1)) | 2u
                | (static_cast<std::uint64_t> (cause) << 8);
            if (value.compare_exchange_strong (cmd.word, terminal, std::memory_order_acq_rel)) return;
        }
    }

private:
    std::uint32_t serial = 0;
    std::atomic<std::uint64_t> value { 0 };
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "Recovery receipt must be lock-free");
}
