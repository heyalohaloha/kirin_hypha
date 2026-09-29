#pragma once

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
    int audible = 0, played = 0, answer = 0;
    std::uint32_t epoch = 0;
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

    BlindCommand command() const noexcept { return { request.load (std::memory_order_acquire) }; }
    bool valid (BlindCommand cmd) const noexcept
    {
        const auto state = terminal.load (std::memory_order_acquire);
        return cmd.active() && (state >> 32) == cmd.epoch() && (state & invalidBit) == 0;
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
        if (! stable || ! valid (cmd) || command().word != cmd.word) return;
        const auto prior = played.load (std::memory_order_relaxed);
        const auto mask = (prior >> 32) == cmd.epoch() ? (prior & 3u) : 0u;
        played.store ((static_cast<std::uint64_t> (cmd.epoch()) << 32)
                      | mask | (cmd.stimulus() == 1 ? 1u : 2u), std::memory_order_release);
        audible.store (cmd.word, std::memory_order_release);
    }

    // Audio Thread or message thread. A concurrent answer can be invalidated, but a newer trial
    // cannot. Bounded retries handle the single answer transition, never wait for a UI thread.
    void invalidate (BlindCommand cmd) noexcept
    {
        auto value = terminal.load (std::memory_order_acquire);
        for (int attempt = 0; attempt < 2 && (value >> 32) == cmd.epoch(); ++attempt)
            if (terminal.compare_exchange_strong (value, value | invalidBit, std::memory_order_acq_rel)) return;
    }

    bool answer (int choice) noexcept
    {
        const auto cmd = command();
        const auto state = view();
        if (! valid (cmd) || state.played != 3 || choice < 1 || choice > 4) return false;
        auto expected = static_cast<std::uint64_t> (cmd.epoch()) << 32;
        return terminal.compare_exchange_strong (expected, expected | static_cast<unsigned> (choice),
                                                 std::memory_order_acq_rel);
    }

    void end() noexcept
    {
        const auto cmd = command();
        invalidate (cmd);
        request.store (cmd.word & ~std::uint64_t (1), std::memory_order_release);
    }

    BlindView view() const noexcept
    {
        const auto cmd = command();
        const auto state = terminal.load (std::memory_order_acquire);
        const auto heard = played.load (std::memory_order_acquire);
        BlindView result;
        result.epoch = cmd.epoch();
        result.active = cmd.active();
        result.invalidated = cmd.active() && ! valid (cmd);
        result.revealed = cmd.active() && ! result.invalidated && (state & 7u) != 0;
        result.firstPre = result.revealed && cmd.firstPre();
        result.answer = result.revealed ? static_cast<int> (state & 7u) : 0;
        result.played = valid (cmd) && (heard >> 32) == cmd.epoch() ? static_cast<int> (heard & 3u) : 0;
        result.audible = valid (cmd) && audible.load (std::memory_order_acquire) == cmd.word ? cmd.stimulus() : 0;
        return result;
    }

private:
    static constexpr std::uint64_t invalidBit = 8u;
    std::uint32_t serial = 0; // message thread only
    std::atomic<std::uint64_t> request { 0 }, terminal { 0 }, played { 0 }, audible { 0 };
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "Blind receipts must be lock-free");
}
