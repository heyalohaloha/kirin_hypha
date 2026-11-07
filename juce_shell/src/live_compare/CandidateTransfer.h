#pragma once
#include "LiveCompareBlockClock.h"
#include <atomic>
#include <cstdint>
#include <limits>
#include <array>
#include <algorithm>

namespace hypha::live_compare
{
// An optional transaction in the existing PRE ring, never a saved pair or a new transport.
// POST owns the request; PRE alone writes the receipt. A callback lease (not an editor timer)
// makes a removed or bypassed POST unable to leave the upstream replacement running.
struct CandidateTransfer
{
    static constexpr std::uint64_t leaseNanos = 500000000;
    std::atomic<std::uint64_t> owner { 0 }, source { 0 }, deadline { 0 }, track { 0 };
    std::atomic<std::uint64_t> sourceOwnerA { 0 }, sourceOwnerB { 0 }, released { 0 };
    std::atomic<std::uint64_t> leaseSequence { 0 }, leaseOwner { 0 };
    std::atomic<std::uint64_t> sequence { 0 }, receiptOwner { 0 }, receiptRun { 0 };
    std::atomic<std::int64_t> start { 0 }, end { 0 };
    std::atomic<std::uint32_t> readers { 0 }; // independent source demand from PRE2 readers
    std::atomic<bool> replacing { false }, hostInactive { false }; // preserve the shared header layout
    std::atomic<bool> bypassed { false };
    // Cleared by the first source mix, including a partial fade; never restored by a gap.
    // A request stays untouched across receiver recreation only if it never changed output.
    std::atomic<std::uint64_t> untouched { 0 };
    void noteOutputChange (std::uint64_t token) noexcept
    { untouched.compare_exchange_strong (token, 0, std::memory_order_acq_rel); }
    // A receipt belongs to its token, not to the slot's current request: another POST can claim
    // this PRE2 while an earlier POST still drains its own return, and neither may erase the
    // other's evidence. PRE2's non-RT owner is the only writer.
    struct Return
    {
        std::atomic<std::uint64_t> sequence { 0 }, token { 0 }, generation { 0 };
        std::atomic<std::int64_t> clock { 0 }, origin { 0 };
        std::atomic<bool> positionValid { false }, untouched { false };
    };
    static constexpr std::size_t returnCount = 8;
    std::array<Return, returnCount> returns;
    std::atomic<std::uint64_t> returnCursor { 0 };
    struct ReturnReceipt
    {
        std::uint64_t generation = 0;
        std::int64_t clock = 0, origin = 0;
        bool positionValid = false, untouched = false;
    };
    // A torn entry reads as absent; the reader's next callback finds it complete.
    bool readReceipt (std::uint64_t token, ReturnReceipt& out) const noexcept
    {
        if (token == 0) return false;
        for (const auto& entry : returns)
        {
            const auto before = entry.sequence.load (std::memory_order_acquire);
            if ((before & 1u) != 0 || entry.token.load (std::memory_order_acquire) != token) continue;
            out = { entry.generation.load(), entry.clock.load(), entry.origin.load(),
                    entry.positionValid.load(), entry.untouched.load() };
            std::atomic_thread_fence (std::memory_order_acquire);
            return entry.sequence.load() == before && entry.token.load() == token;
        }
        return false;
    }
    bool withdrawn (std::uint64_t token) const noexcept
    {
        ReturnReceipt receipt;
        return readReceipt (token, receipt);
    }
    bool returnedUntouched (std::uint64_t token) const noexcept
    {
        ReturnReceipt receipt;
        return readReceipt (token, receipt) && receipt.untouched;
    }
    void acknowledgeReturn (std::uint64_t token, const BlockClock& block, std::uint64_t generation = 0) noexcept
    {
        std::int64_t origin = 0, boundary = 0;
        const bool valid = block.clockValid && block.projectValid
            && checkedClockSubtract (block.clock, block.project, origin)
            && checkedClockAdd (block.clock, block.frames, boundary);
        Return* slot = nullptr;
        for (auto& entry : returns) if (entry.token.load() == token) slot = &entry;
        for (auto& entry : returns) if (slot == nullptr && entry.token.load() == 0) slot = &entry;
        if (slot == nullptr) slot = &returns[returnCursor.fetch_add (1, std::memory_order_relaxed) % returnCount];
        slot->sequence.fetch_add (1, std::memory_order_acq_rel);
        slot->generation.store (generation, std::memory_order_relaxed);
        slot->clock.store (boundary, std::memory_order_relaxed);
        slot->origin.store (origin, std::memory_order_relaxed);
        slot->positionValid.store (valid, std::memory_order_relaxed);
        slot->untouched.store (untouched.load (std::memory_order_acquire) == token, std::memory_order_relaxed);
        slot->token.store (token, std::memory_order_release);
        replacing.store (false, std::memory_order_release);
        slot->sequence.fetch_add (1, std::memory_order_release);
    }
    // The same request is replacing again, so its earlier withdrawal is no longer true.
    void revokeReturn (std::uint64_t token) noexcept
    {
        for (auto& entry : returns)
            if (entry.token.load() == token)
            {
                entry.sequence.fetch_add (1, std::memory_order_acq_rel);
                entry.token.store (0, std::memory_order_release);
                entry.sequence.fetch_add (1, std::memory_order_release);
            }
    }
    void clearReturns() noexcept
    {
        for (auto& entry : returns) { entry.sequence.store (0); entry.token.store (0); }
        returnCursor.store (0);
    }
    bool readReturn (std::uint64_t token, std::int64_t& boundary, std::int64_t& origin,
                     std::uint64_t& generation) const noexcept
    {
        ReturnReceipt receipt;
        if (! readReceipt (token, receipt) || ! receipt.positionValid) return false;
        boundary = receipt.clock; origin = receipt.origin; generation = receipt.generation;
        return true;
    }
    enum class Lease { valid, updating, expired };
    struct Range
    {
        std::atomic<std::uint64_t> sequence { 0 }, owner { 0 }, run { 0 };
        std::atomic<std::int64_t> start { 0 }, end { 0 };
    };
    // Contiguous spans survive a later hole. Extending a span only publishes its end;
    // the reader of an already-written delayed block does not race every new callback.
    static constexpr std::size_t rangeCount = 1024;
    std::array<Range, rangeCount> ranges;
    std::atomic<std::uint64_t> published { 0 };

    // Publish the complete request under the same bounded writer claim as the heartbeat.
    // A second POST cannot steal the gap between claiming a token and publishing its source.
    bool begin (std::uint64_t token, std::uint64_t input, std::uint64_t inputOwnerA,
                std::uint64_t inputOwnerB, std::uint64_t now) noexcept
    {
        auto before = leaseSequence.load (std::memory_order_acquire);
        if (token == 0 || input == 0 || (before & 1u) != 0
            || !leaseSequence.compare_exchange_strong (before, before + 1, std::memory_order_acq_rel)) return false;
        const auto previous = owner.load (std::memory_order_acquire);
        const auto until = deadline.load (std::memory_order_relaxed);
        const bool expired = leaseOwner.load (std::memory_order_relaxed) == previous && now > until;
        const bool vacant = previous == 0 || ((expired || released.load() == previous) && !replacing.load());
        if (vacant)
        {
            untouched.store (token, std::memory_order_release);
            source.store (input, std::memory_order_relaxed);
            sourceOwnerA.store (inputOwnerA, std::memory_order_relaxed);
            sourceOwnerB.store (inputOwnerB, std::memory_order_relaxed);
            released.store (0, std::memory_order_relaxed);
            deadline.store (now + leaseNanos, std::memory_order_relaxed);
            leaseOwner.store (token, std::memory_order_relaxed);
            owner.store (token, std::memory_order_release);
        }
        leaseSequence.store (before + 2, std::memory_order_release);
        return vacant;
    }

    // A bounded writer claim also handles a final callback from an old POST. It can neither
    // renew a newer request nor splice its deadline into that request's heartbeat.
    bool refreshLease (std::uint64_t token, std::uint64_t now) noexcept
    {
        auto before = leaseSequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0 || ! leaseSequence.compare_exchange_strong (before, before + 1,
                std::memory_order_acq_rel)) return false;
        const bool current = owner.load (std::memory_order_acquire) == token && token != 0;
        if (current)
        {
            deadline.store (now == 0 ? 0 : now + leaseNanos, std::memory_order_relaxed);
            leaseOwner.store (token, std::memory_order_relaxed);
        }
        leaseSequence.store (before + 2, std::memory_order_release);
        return current;
    }
    Lease readLease (std::uint64_t token, std::uint64_t now, std::uint64_t& until) const noexcept
    {
        const auto before = leaseSequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0) return Lease::updating;
        until = deadline.load (std::memory_order_relaxed);
        const bool valid = token != 0 && owner.load (std::memory_order_acquire) == token
            && leaseOwner.load (std::memory_order_relaxed) == token
            && released.load (std::memory_order_acquire) != token && until != 0 && now <= until;
        std::atomic_thread_fence (std::memory_order_acquire);
        if (leaseSequence.load (std::memory_order_relaxed) != before) return Lease::updating;
        return valid ? Lease::valid : Lease::expired;
    }
    bool leased (std::uint64_t token, std::uint64_t now) const noexcept
    {
        std::uint64_t until = 0;
        return readLease (token, now, until) == Lease::valid;
    }
    bool settled (std::uint64_t token, std::uint64_t run, std::int64_t first,
                  std::int64_t last, std::uint32_t rate) const noexcept
    {
        // The first full four seconds of the new B input are excluded from MATCH. This is a
        // settling allowance, not a claim that an arbitrary external processor has no tail.
        const auto allowance = static_cast<std::int64_t> (rate) * 4;
        return first >= std::numeric_limits<std::int64_t>::min() + allowance
            && covers (token, run, first - allowance, last);
    }
    void publish (std::uint64_t token, std::uint64_t run, std::int64_t first,
                  std::int64_t last, bool active) noexcept
    {
        if (token != 0 && last > first)
        {
            const auto count = published.load (std::memory_order_relaxed);
            auto& previous = ranges[(count == 0 ? 0 : count - 1) % rangeCount];
            if (count != 0 && previous.owner.load() == token && previous.run.load() == run
                && previous.start.load() == first && previous.end.load() <= last)
                previous.end.store (last, std::memory_order_release);
            else
            {
                auto& range = ranges[count % rangeCount];
                range.sequence.fetch_add (1, std::memory_order_acq_rel);
                range.owner.store (token); range.run.store (run);
                range.start.store (first); range.end.store (last);
                range.sequence.fetch_add (1, std::memory_order_release);
                published.store (count + 1, std::memory_order_release);
            }
        }
        sequence.fetch_add (1, std::memory_order_acq_rel);
        receiptOwner.store (token, std::memory_order_relaxed);
        receiptRun.store (run, std::memory_order_relaxed);
        start.store (first, std::memory_order_relaxed);
        end.store (last, std::memory_order_relaxed);
        replacing.store (active, std::memory_order_relaxed);
        sequence.fetch_add (1, std::memory_order_release);
    }
    std::int64_t contiguousFrames (std::uint64_t token, std::uint64_t run, std::int64_t last) const noexcept
    {
        const auto count = published.load (std::memory_order_acquire);
        for (std::uint64_t i = 0; i < std::min<std::uint64_t> (count, rangeCount); ++i)
        {
            const auto& range = ranges[(count - i - 1) % rangeCount];
            const auto before = range.sequence.load (std::memory_order_acquire);
            const auto first = range.start.load();
            const bool valid = (before & 1u) == 0 && range.owner.load() == token && range.run.load() == run
                && last > first && last <= range.end.load (std::memory_order_acquire);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (valid && range.sequence.load() == before) return last - first;
        }
        return 0;
    }
    bool covers (std::uint64_t token, std::uint64_t run, std::int64_t first,
                 std::int64_t last) const noexcept
    {
        if (token == 0 || last <= first) return false;
        const auto count = published.load (std::memory_order_acquire);
        for (std::uint64_t i = 0; i < std::min<std::uint64_t> (count, rangeCount); ++i)
        {
            const auto& range = ranges[(count - i - 1) % rangeCount];
            const auto before = range.sequence.load (std::memory_order_acquire);
            if ((before & 1u) != 0) continue;
            const bool valid = range.owner.load() == token && range.run.load() == run
                && first >= range.start.load() && last <= range.end.load (std::memory_order_acquire);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (valid && range.sequence.load() == before) return true;
        }
        return false;
    }
};
#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
inline void (*afterCandidateReceiverReadForTest)() = nullptr;
inline void (*afterLiveCompareServiceReadForTest)() = nullptr;
inline void (*afterLiveBlindScopeAcquiredForTest)() = nullptr;
inline void (*afterLiveBlindRestoreFenceForTest)() = nullptr;
#endif
}
