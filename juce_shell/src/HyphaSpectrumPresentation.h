#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace hypha::spectrum_presentation
{
// Display-only spatial calm. It has no history, state, timer, or audio-thread dependency.
inline constexpr float lowCalmFullEndHz = 70.0f;
inline constexpr float lowCalmTaperEndHz = 180.0f;
inline constexpr float lowCalmMaximumBlend = 0.55f;

inline float lowFrequencyCalmBlend (float frequencyHz) noexcept
{
    if (frequencyHz <= lowCalmFullEndHz)
        return lowCalmMaximumBlend;
    if (frequencyHz >= lowCalmTaperEndHz)
        return 0.0f;

    const float position = (frequencyHz - lowCalmFullEndHz)
                         / (lowCalmTaperEndHz - lowCalmFullEndHz);
    const float smoothStep = position * position * (3.0f - 2.0f * position);
    return lowCalmMaximumBlend * (1.0f - smoothStep);
}

template <std::size_t Size>
std::array<float, Size> lowFrequencyCalmWeights (float minimumHz,
                                                 float maximumHz) noexcept
{
    static_assert (Size > 1u);
    std::array<float, Size> weights {};
    if (! std::isfinite (minimumHz) || ! std::isfinite (maximumHz)
        || minimumHz <= 0.0f || maximumHz <= minimumHz)
        return weights;

    const float ratio = std::pow (maximumHz / minimumHz,
                                  1.0f / static_cast<float> (Size));
    float frequencyHz = minimumHz * std::sqrt (ratio);
    for (std::size_t index = 0; index < Size; ++index)
    {
        weights[index] = lowFrequencyCalmBlend (frequencyHz);
        if (weights[index] <= 0.0f)
            break;
        frequencyHz *= ratio;
    }
    return weights;
}

template <std::size_t Size>
std::array<float, Size> calmLowFrequencies (
    const float (&input)[Size],
    const std::array<float, Size>& blendWeights) noexcept
{
    static_assert (Size >= 5u);
    std::array<float, Size> output {};
    const auto sample = [&input] (std::ptrdiff_t index) {
        const auto last = static_cast<std::ptrdiff_t> (Size - 1u);
        return input[static_cast<std::size_t> (std::clamp (index, std::ptrdiff_t { 0 }, last))];
    };

    for (std::size_t index = 0; index < Size; ++index)
    {
        const float blend = blendWeights[index];
        if (blend <= 0.0f)
        {
            output[index] = input[index];
            continue;
        }

        const auto centre = static_cast<std::ptrdiff_t> (index);
        const float neighbourAverage = (sample (centre - 2) + 2.0f * sample (centre - 1)
                                      + 3.0f * sample (centre)
                                      + 2.0f * sample (centre + 1) + sample (centre + 2))
                                     / 9.0f;
        output[index] = input[index] + blend * (neighbourAverage - input[index]);
    }
    return output;
}
}
