#pragma once
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// One owner publishes both amounts; the Audio Thread snapshots them together and only stores receipts.
class AlignWatch
{
public:
    struct Snapshot
    {
        std::int64_t kept, next;
        std::uint16_t epoch;
    };
    Snapshot snapshot() const noexcept
    {
        const auto word = watched.load();
        return { static_cast<std::int64_t> (word & mask), static_cast<std::int64_t> ((word >> 24) & mask),
                 static_cast<std::uint16_t> (word >> 48) };
    }
    void recordKept (std::uint16_t epoch) noexcept { keptReceipt.store (receipt (epoch)); }
    void recordNext (std::uint16_t epoch) noexcept { nextReceipt.store (receipt (epoch)); }
    void watchNext (std::int64_t frames) noexcept
    {
        const auto s = snapshot();
        pending = bounded (frames);
        pendingEpoch = publish (s.kept, pending);
    }
    void watchKept (std::int64_t frames) noexcept
    {
        const auto value = bounded (frames);
        promoted = value != 0 && pending == value;
        promotedEpoch = pendingEpoch;
        pending = 0;
        kept = value;
        keptEpoch = publish (value, 0);
    }
    std::int64_t takenOver() noexcept
    {
        const auto k = keptReceipt.exchange (0);
        const auto n = nextReceipt.exchange (0);
        // A kept receipt belongs to this correction from its publication until the current word.
        // A next receipt belongs to it only when that precise pending publication was promoted.
        const auto end = snapshot().epoch;
        const auto inKeptSpan = [&] (std::uint64_t r) {
            return r != 0 && static_cast<std::uint16_t> ((r >> 1) - keptEpoch)
                <= static_cast<std::uint16_t> (end - keptEpoch);
        };
        return kept != 0 && (inKeptSpan (k) || (promoted && n == receipt (promotedEpoch))) ? kept : 0;
    }
private:
    static constexpr std::uint64_t mask = (std::uint64_t { 1 } << 24) - 1;
    static std::int64_t bounded (std::int64_t frames) noexcept
    { return frames > 0 && static_cast<std::uint64_t> (frames) <= mask ? frames : 0; }
    static std::uint64_t receipt (std::uint16_t epoch) noexcept
    { return (static_cast<std::uint64_t> (epoch) << 1) | 1; }
    std::uint16_t publish (std::int64_t a, std::int64_t b) noexcept
    {
        const auto epoch = static_cast<std::uint16_t> (snapshot().epoch + 1);
        watched.store ((static_cast<std::uint64_t> (epoch) << 48)
                       | (static_cast<std::uint64_t> (b) << 24) | static_cast<std::uint64_t> (a));
        return epoch;
    }
    std::atomic<std::uint64_t> watched { 0 }, keptReceipt { 0 }, nextReceipt { 0 };
    // Owner thread only.
    std::int64_t kept = 0, pending = 0;
    std::uint16_t keptEpoch = 0, pendingEpoch = 0, promotedEpoch = 0;
    bool promoted = false;
};
}
