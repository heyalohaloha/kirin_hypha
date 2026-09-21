#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "kirin_hypha_ffi.h"

// Six seconds of MONO, one entry per exact 100 ms observation.
//
// The Meter Session publishes 32 bands at 10 Hz and keeps no history of them: a 32-band array in
// the 10 Hz TIME history would hold 192,000 values where correlation holds 6,000. The six seconds
// a user can see live are held here instead, which is why closing the editor loses them.
namespace hypha::mono_sum_history
{
inline constexpr size_t capacity = 60u;
inline constexpr double seconds = 6.0;

struct Entry
{
    uint64_t observedFrames = 0;
    uint32_t sampleRate = 0;
    std::array<float, KIRIN_MONO_SUM_BAND_COUNT> db {};
};

class History
{
public:
    /// Appends one observation. Returns false when nothing was stored, which is every case that
    /// is not a new exact observation carrying measured bands.
    bool append (const KirinMeterSession& meter) noexcept
    {
        if (meter.mono_sum_band_count != KIRIN_MONO_SUM_BAND_COUNT || meter.sample_rate == 0u)
            return false;

        if (count > 0u)
        {
            const auto& newest = at (count - 1u);
            // The same observation arriving again is not a new row. A session reset or a rate
            // change makes the stored ages meaningless, so the field starts over rather than
            // mixing two timelines.
            if (meter.observed_frames == newest.observedFrames)
                return false;
            if (meter.observed_frames < newest.observedFrames
                || meter.sample_rate != newest.sampleRate
                || meter.generation != generation
                || meter.measurement_epoch != measurementEpoch)
                clear();
        }
        generation = meter.generation;
        measurementEpoch = meter.measurement_epoch;

        const auto destination = count < capacity ? (start + count) % capacity : start;
        auto& entry = frames[destination];
        entry.observedFrames = meter.observed_frames;
        entry.sampleRate = meter.sample_rate;
        for (size_t band = 0u; band < KIRIN_MONO_SUM_BAND_COUNT; ++band)
            entry.db[band] = meter.mono_sum_db[band];
        if (count < capacity)
            ++count;
        else
            start = (start + 1u) % capacity;
        return true;
    }

    void clear() noexcept
    {
        start = 0u;
        count = 0u;
    }

    bool empty() const noexcept { return count == 0u; }
    size_t size() const noexcept { return count; }

    /// Oldest first, so ages fall as the index rises.
    const Entry& at (size_t index) const noexcept
    {
        return frames[(start + (index < count ? index : count - 1u)) % capacity];
    }

    /// Age of `index` against the newest entry, in seconds.
    double ageSeconds (size_t index) const noexcept
    {
        if (count == 0u)
            return seconds;
        const auto& newest = at (count - 1u);
        const auto& entry = at (index);
        if (entry.sampleRate == 0u || newest.observedFrames < entry.observedFrames)
            return seconds;
        return (double) (newest.observedFrames - entry.observedFrames)
             / (double) entry.sampleRate;
    }

private:
    std::array<Entry, capacity> frames {};
    size_t start = 0u;
    size_t count = 0u;
    uint64_t measurementEpoch = 0;
    uint64_t generation = 0;
};
}
