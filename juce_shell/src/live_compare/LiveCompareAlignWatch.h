#pragma once
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
#ifdef KIRIN_ALIGN_WATCH_TEST
inline void (*betweenAlignWatchLoadsForTest)() = nullptr;
#endif
// One owner, one Audio Thread. Identity is 39 bits, never recycled during this object lifetime.
// At 100 new amounts/second, the identity space lasts more than 170 years.
class AlignWatch
{
public:
    struct Snapshot { std::uint64_t next, kept; };
    static std::int64_t frames (std::uint64_t word) noexcept { return static_cast<std::int64_t> (word & mask); }
    // Audio Thread. next first, kept second: with the owner's order below, an amount that is
    // promoted is seen in at least one of the two.
    Snapshot snapshot() const noexcept
    {
        const auto n = nextWord.load();
       #ifdef KIRIN_ALIGN_WATCH_TEST
        if (betweenAlignWatchLoadsForTest) betweenAlignWatchLoadsForTest();
       #endif
        return { n, keptWord.load() };
    }
    // Audio Thread: a step of K equalled the amount in `word`. One CAS, for that amount's identity only.
    void record (std::uint64_t word) noexcept
    {
        if (word == 0) return;
        auto armed = (word >> 25) << 1;
        cells[(word >> 24) & 1u].compare_exchange_strong (armed, armed | 1u);
    }
    // Owner thread.
    void watchNext (std::int64_t amount) noexcept
    {
        pending = bounded (amount);
        if (pending == 0) { pendingId = 0; nextWord.store (0); return; }
        pendingId = identity(); pendingLane = keptLane ^ 1u;   // never kept's cell
        if (pendingId == 0) { pending = 0; nextWord.store (0); return; }
        cells[pendingLane].store (pendingId << 1);            // armed before it is published
        nextWord.store (word (pending, pendingLane, pendingId));
    }
    void watchKept (std::int64_t amount) noexcept
    {
        auto value = bounded (amount);
        if (value != 0 && pending == value) { keptId = pendingId; keptLane = pendingLane; } // the same identity, hits included
        else if (value == 0) keptId = 0;
        else { keptId = identity(); if (keptId == 0) value = 0; cells[keptLane].store (keptId << 1); }
        kept = value; pending = 0; pendingId = 0;
        keptWord.store (value != 0 ? word (kept, keptLane, keptId) : 0); // kept first,
        nextWord.store (0);                                              // then next
    }
    std::int64_t takenOver() noexcept
    {
        if (kept == 0) return 0;
        auto hit = (keptId << 1) | 1u;
        return cells[keptLane].compare_exchange_strong (hit, keptId << 1) ? kept : 0;
    }
private:
    // Exhaustion disables new watches, never aliases a historic receipt.
    std::uint64_t identity() noexcept { return lastId < ((std::uint64_t { 1 } << 39) - 1) ? ++lastId : 0; }
    static constexpr std::uint64_t mask = (std::uint64_t { 1 } << 24) - 1;
    static std::int64_t bounded (std::int64_t f) noexcept { return f > 0 && static_cast<std::uint64_t> (f) <= mask ? f : 0; }
    static std::uint64_t word (std::int64_t amount, std::uint64_t lane, std::uint64_t id) noexcept
    { return (id << 25) | (lane << 24) | static_cast<std::uint64_t> (amount); }
    std::atomic<std::uint64_t> nextWord { 0 }, keptWord { 0 }, cells[2] { { 0 }, { 0 } };
    // Owner thread only.
    std::int64_t kept = 0, pending = 0;
    std::uint64_t keptId = 0, pendingId = 0, lastId = 0, keptLane = 0, pendingLane = 1;
};
}
