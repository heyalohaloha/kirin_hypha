#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "kirin_hypha_ffi.h"

namespace hypha::absolute_spectrum
{
constexpr size_t historyCapacity = 180u;
constexpr double historySeconds = 6.0;

struct Frame
{
    int64_t endpoint = 0;
    uint32_t sampleRate = 0;
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> postDbfs {};
};

class History
{
public:
    History() noexcept { peak.fill (-std::numeric_limits<float>::infinity()); }

    bool append (const KirinSpectrumView& view) noexcept
    {
        if (view.post_has_data == 0 || view.sample_rate < 8'000u
            || view.presentation_end_samples <= 0
            || ! std::all_of (std::begin (view.post_dbfs), std::end (view.post_dbfs),
                              [] (float value) { return std::isfinite (value); }))
            return false;

        if (count > 0u)
        {
            const auto& newest = at (count - 1u);
            if (view.presentation_end_samples == newest.endpoint)
                return false;
            if (view.presentation_end_samples < newest.endpoint
                || view.sample_rate != newest.sampleRate)
                clear();
        }

        const bool replacing = count == historyCapacity;
        const size_t destination = count < historyCapacity
            ? (start + count) % historyCapacity : start;
        const auto evicted = replacing ? frames[destination].postDbfs
                                       : std::array<float, KIRIN_SPECTRUM_BAND_COUNT> {};
        frames[destination] = {
            view.presentation_end_samples,
            view.sample_rate,
            {},
        };
        std::copy (std::begin (view.post_dbfs), std::end (view.post_dbfs),
                   frames[destination].postDbfs.begin());
        if (count < historyCapacity)
            ++count;
        else
            start = (start + 1u) % historyCapacity;
        for (size_t band = 0u; band < peak.size(); ++band)
        {
            const auto incoming = view.post_dbfs[band];
            if (! replacing || incoming >= peak[band])
                peak[band] = std::max (peak[band], incoming);
            else if (evicted[band] >= peak[band])
                recomputePeakBand (band);
        }
        return true;
    }

    void clear() noexcept
    {
        start = 0u;
        count = 0u;
        peak.fill (-std::numeric_limits<float>::infinity());
    }

    bool empty() const noexcept { return count == 0u; }
    size_t size() const noexcept { return count; }

    const Frame& at (size_t logicalIndex) const noexcept
    {
        const size_t bounded = count > 0u ? std::min (logicalIndex, count - 1u) : 0u;
        return frames[(start + bounded) % historyCapacity];
    }

    const std::array<float, KIRIN_SPECTRUM_BAND_COUNT>& peakHold() const noexcept
    {
        return peak;
    }

private:
    void recomputePeakBand (size_t band) noexcept
    {
        peak[band] = -std::numeric_limits<float>::infinity();
        for (size_t frameIndex = 0u; frameIndex < count; ++frameIndex)
            peak[band] = std::max (peak[band], at (frameIndex).postDbfs[band]);
    }

    std::array<Frame, historyCapacity> frames {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> peak {};
    size_t start = 0u;
    size_t count = 0u;
};
}
