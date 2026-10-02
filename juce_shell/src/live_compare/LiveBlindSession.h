#pragma once

#include "LiveCompareRecovery.h"
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
struct BlindCommand
{
    std::uint64_t word = 0;
    std::uint32_t epoch() const noexcept { return static_cast<std::uint32_t> (word >> 32); }
    bool active() const noexcept { return (word & 1) != 0; }
    int stimulus() const noexcept { return (word & 2) != 0 ? 2 : 1; }
    bool firstPre() const noexcept { return (word & 4) != 0; }
    bool pre() const noexcept { return (stimulus() == 1) == firstPre(); }
};

struct BlindView
{
    bool active = false, revealed = false, invalidated = false, firstPre = false;
    int audible = 0, played = 0;
    std::uint32_t epoch = 0;
    RecoveryReason reason = RecoveryReason::none;
};

// The command is one atomic word, sampled once per audio block. Receipts retain that exact word.
// Mapping is supplied by the non-RT OS CSPRNG caller. UI never labels a request as actual output.
class BlindSession
{
public:
    // Non-RT only: failed OS randomness must never publish a predictable fallback assignment.
    template <typename RandomBit>
    bool startWith (RandomBit&& randomBit) noexcept
    {
        try { start (randomBit()); return true; }
        catch (...) { end(); return false; }
    }

    void start (bool firstPre) noexcept
    {
        const auto next = (static_cast<std::uint64_t> (++serial) << 32);
        terminal.store (next, std::memory_order_release);
        request.store (next | 1u | (firstPre ? 4u : 0u), std::memory_order_release);
    }

    void reset() noexcept // a new preparation, or acknowledged END; message thread only
    {
        const auto next = static_cast<std::uint64_t> (++serial) << 32;
        terminal.store (next, std::memory_order_release);
        request.store (next, std::memory_order_release);
    }

    BlindCommand command() const noexcept { return { request.load (std::memory_order_acquire) }; }
    bool valid (BlindCommand cmd) const noexcept
    {
        const auto state = terminal.load (std::memory_order_acquire);
        return cmd.active() && (state >> 32) == cmd.epoch() && (state & (invalidBit | endedBit)) == 0;
    }

    bool select (int stimulus) noexcept
    {
        auto current = command();
        if (! valid (current) || stimulus < 1 || stimulus > 2) return false;
        // Increment the sequence even when the same source is requested again.
        const auto next = ((current.word + 8u) & ~std::uint64_t (2)) | (stimulus == 2 ? 2u : 0u);
        return request.compare_exchange_strong (current.word, next, std::memory_order_acq_rel);
    }

    // Audio Thread only. A stable sample, not a mixed crossfade or a safety POST fallback.
    void observe (BlindCommand cmd, bool stable) noexcept
    {
        if (! valid (cmd) || command().word != cmd.word) return;
        if (! stable)
        {
            // A receipt describes the block that is audible now, not merely a source heard
            // earlier in the trial. LOOP revalidation and a same-command crossfade may
            // temporarily return to POST; retaining the old word would make the UI and audit
            // claim that the requested anonymous source still sounds.
            auto heard = cmd.word;
            audible.compare_exchange_strong (heard, 0, std::memory_order_acq_rel);
            return;
        }
        const auto prior = played.load (std::memory_order_relaxed);
        const auto mask = (prior >> 32) == cmd.epoch() ? (prior & 3u) : 0u;
        played.store ((static_cast<std::uint64_t> (cmd.epoch()) << 32)
                      | mask | (cmd.stimulus() == 1 ? 1u : 2u), std::memory_order_release);
        audible.store (cmd.word, std::memory_order_release);
    }

    // Audio Thread or message thread. A concurrent reveal can be invalidated, but a newer trial
    // cannot. Bounded retries handle the single reveal transition, never wait for a UI thread.
    void invalidate (BlindCommand cmd, RecoveryReason reason) noexcept
    {
        auto value = terminal.load (std::memory_order_acquire);
        for (int attempt = 0; attempt < 2 && cmd.active() && (value >> 32) == cmd.epoch()
             && (value & (invalidBit | endedBit)) == 0; ++attempt)
        {
            const auto failed = value | invalidBit | (static_cast<std::uint64_t> (
                reason == RecoveryReason::none ? RecoveryReason::unknown : reason) << 8);
            if (terminal.compare_exchange_strong (value, failed, std::memory_order_acq_rel)) return;
        }
    }

    bool reveal() noexcept
    {
        const auto cmd = command();
        const auto state = view();
        if (! valid (cmd) || state.played != 3) return false;
        auto expected = static_cast<std::uint64_t> (cmd.epoch()) << 32;
        return terminal.compare_exchange_strong (expected, expected | revealedBit,
                                                 std::memory_order_acq_rel);
    }

    void end() noexcept
    {
        const auto cmd = command();
        auto value = terminal.load (std::memory_order_acquire);
        for (int attempt = 0; attempt < 2 && (value >> 32) == cmd.epoch(); ++attempt)
            if (terminal.compare_exchange_strong (value, value | endedBit, std::memory_order_acq_rel)) break;
        request.store (cmd.word & ~std::uint64_t (1), std::memory_order_release);
    }

    BlindView view() const noexcept
    {
        const auto cmd = command();
        const auto state = terminal.load (std::memory_order_acquire);
        const auto heard = played.load (std::memory_order_acquire);
        BlindView result;
        result.epoch = cmd.epoch();
        result.reason = (state >> 32) == cmd.epoch()
            ? static_cast<RecoveryReason> ((state >> 8) & 0xffu) : RecoveryReason::none;
        result.active = cmd.active();
        result.invalidated = cmd.active() && ! valid (cmd);
        result.revealed = cmd.active() && ! result.invalidated && (state & revealedBit) != 0;
        result.firstPre = result.revealed && cmd.firstPre();
        result.played = valid (cmd) && (heard >> 32) == cmd.epoch() ? static_cast<int> (heard & 3u) : 0;
        result.audible = valid (cmd) && audible.load (std::memory_order_acquire) == cmd.word ? cmd.stimulus() : 0;
        return result;
    }

private:
    static constexpr std::uint64_t revealedBit = 1u;
    static constexpr std::uint64_t invalidBit = 8u;
    static constexpr std::uint64_t endedBit = 16u;
    std::uint32_t serial = 0; // message thread only
    std::atomic<std::uint64_t> request { 0 }, terminal { 0 }, played { 0 }, audible { 0 };
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "Blind receipts must be lock-free");
}
