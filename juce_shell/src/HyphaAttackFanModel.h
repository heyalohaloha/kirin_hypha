#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include "kirin_hypha_ffi.h"

namespace hypha::attack_fan
{
struct Motion { std::array<float, 7> bend {}; };
inline float unit (float value) noexcept
{ return std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f; }

// Presentation only: fixed taps of the existing envelope, no oscillator or replay clock.
inline Motion measuredMotion (const KirinAttackWaveformBatch& batch,
                              std::int64_t latest, std::uint32_t rate,
                              std::uint64_t generation) noexcept
{
    Motion motion;
    if (rate == 0 || latest < std::numeric_limits<std::int64_t>::min() + rate)
        return motion;
    const auto count = std::min (batch.count,
        static_cast<std::uint32_t> (KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY));
    std::array<float, 8> envelope {};
    std::array<bool, 8> valid {};
    std::array<std::uint32_t, 8> indices {};
    const auto usable = [=] (const auto& p) {
        return p.generation == generation && p.sample_rate == rate
            && p.channels >= 1 && p.channels <= 2 && std::isfinite (p.rms_dbfs)
            && p.end_sample > p.start_sample; };
    for (std::size_t tap = 0; tap < envelope.size(); ++tap)
    {
        const auto sample = latest - 1 - static_cast<std::int64_t> (tap * rate / 50);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto& p = batch.points[i];
            if (! usable (p) || sample < p.start_sample || sample >= p.end_sample) continue;
            envelope[tap] = unit ((p.rms_dbfs + 72.0f) / 72.0f);
            if (i > 0 && usable (batch.points[i - 1])
                && batch.points[i - 1].channels == p.channels && batch.points[i - 1].end_sample == p.start_sample)
            {
                const auto previous = unit ((batch.points[i - 1].rms_dbfs + 72.0f) / 72.0f);
                const auto fraction = static_cast<float> ((static_cast<long double> (sample) - p.start_sample)
                    / (static_cast<long double> (p.end_sample) - p.start_sample));
                envelope[tap] = previous + (envelope[tap] - previous) * fraction;
            }
            valid[tap] = true;
            indices[tap] = i;
            break;
        }
    }
    for (std::size_t i = 0; i < motion.bend.size(); ++i)
        if (valid[i] && valid[i + 1] && indices[i + 1] <= indices[i])
        {
            bool contiguous = true;
            for (auto j = indices[i + 1] + 1; j <= indices[i]; ++j)
                contiguous = contiguous && usable (batch.points[j])
                    && batch.points[j - 1].end_sample == batch.points[j].start_sample
                    && batch.points[j - 1].channels == batch.points[j].channels;
            if (contiguous) motion.bend[i] = (envelope[i] - envelope[i + 1]) * 0.24f;
        }
    return motion;
}
}
