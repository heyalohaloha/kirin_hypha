#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hypha::spectrum_presentation
{
// Display-only spatial calm. It has no history, state, timer, or audio-thread dependency.
inline constexpr float lowCalmFullEndHz = 70.0f;
inline constexpr float lowCalmTaperEndHz = 180.0f;
inline constexpr float lowCalmMaximumBlend = 0.55f;
// Live absolute curves rise on the next existing presentation tick, then fall at a stable
// display-only rate. Signed delta cannot use that asymmetric rule without favouring one sign,
// so it follows the same target in both directions with a short time constant.
inline constexpr float absoluteReleaseDb = 20.0f;
inline constexpr double absoluteReleaseDurationMs = 500.0;
inline constexpr double signedDeltaResponseMs = 150.0;

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

// Opt-in smoothing whose width grows with frequency: the chosen width up to 200 Hz, four times
// that at 8 kHz and above (never past 1/2 octave), joined by a smoothstep over log frequency.
inline constexpr float wideningFromHz = 200.0f, wideningToHz = 8000.0f;
inline constexpr float wideningRatio = 4.0f, wideningMaximumOctave = 0.5f;
inline float wideningOctaveAt (float frequencyHz, float lowOctave) noexcept
{
    if (! (lowOctave > 0.0f) || ! (frequencyHz > 0.0f)) return 0.0f;
    const float position = std::clamp (std::log2 (frequencyHz / wideningFromHz)
                                       / std::log2 (wideningToHz / wideningFromHz), 0.0f, 1.0f);
    const float eased = position * position * (3.0f - 2.0f * position);
    return std::min (std::max (lowOctave, wideningMaximumOctave),
                     lowOctave * (1.0f + (wideningRatio - 1.0f) * eased));
}
// Half width in bands as it enters the triangle weights: nothing below half a band, then a
// continuous rise, so the texture of the line never changes in a step along the axis.
inline float wideningHalfWidth (float halfBands) noexcept
{ return ! (halfBands > 0.5f) ? 0.0f : halfBands < 1.0f ? 2.0f * halfBands - 1.0f : halfBands; }

template <std::size_t Size>
std::array<float, Size> smoothWidening (const std::array<float, Size>& source, float minimumHz, float maximumHz,
                                        float lowOctave, const std::array<uint8_t, Size>* valid = nullptr) noexcept
{
    auto output = source;
    if (! std::isfinite (minimumHz) || ! std::isfinite (maximumHz) || minimumHz <= 0.0f || maximumHz <= minimumHz)
        return output;
    const float bandsPerOctave = static_cast<float> (Size) / std::log2 (maximumHz / minimumHz);
    const float ratio = std::pow (maximumHz / minimumHz, 1.0f / static_cast<float> (Size));
    float frequencyHz = minimumHz * std::sqrt (ratio);
    for (std::size_t index = 0; index < Size; ++index, frequencyHz *= ratio)
    {
        if (valid != nullptr && (*valid)[index] == 0u) continue;
        const float half = wideningHalfWidth (bandsPerOctave * wideningOctaveAt (frequencyHz, lowOctave) * 0.5f);
        if (! (half > 0.0f)) continue;
        float sum = source[index], weightSum = 1.0f;
        for (int direction : { -1, 1 })
            for (int step = 1; static_cast<float> (step) < half + 1.0f; ++step)
            {
                const auto at = static_cast<std::ptrdiff_t> (index) + direction * step;
                // A missing band has no dB value: the average stops there, as the fixed widths do.
                if (at < 0 || at >= static_cast<std::ptrdiff_t> (Size)
                    || (valid != nullptr && (*valid)[static_cast<std::size_t> (at)] == 0u)) break;
                const float weight = 1.0f - static_cast<float> (step) / (half + 1.0f);
                sum += source[static_cast<std::size_t> (at)] * weight; weightSum += weight;
            }
        output[index] = sum / weightSum;
    }
    return output;
}

template <std::size_t Size>
void advanceAverageCurve (std::array<float, Size>& displayed,
                          const std::array<float, Size>& target,
                          double elapsedMs, double averageMs) noexcept
{
    if (! std::isfinite (elapsedMs) || elapsedMs <= 0.0 || ! std::isfinite (averageMs) || averageMs <= 0.0) return;
    const float response = static_cast<float> (-std::expm1 (-elapsedMs / averageMs));
    for (std::size_t index = 0; index < Size; ++index)
    {
        if (! std::isfinite (target[index])) continue;
        if (! std::isfinite (displayed[index])) displayed[index] = target[index];
        else displayed[index] += response * (target[index] - displayed[index]);
    }
}

template <std::size_t Size>
void advanceAbsoluteCurve (std::array<float, Size>& displayed,
                           const std::array<float, Size>& target,
                           double elapsedMs, double averageMs = 0.0, bool smoothAttack = false,
                           float releaseDbPerSecond = absoluteReleaseDb * 1000.0f / (float) absoluteReleaseDurationMs) noexcept
{
    const double boundedElapsedMs = std::isfinite (elapsedMs) && elapsedMs > 0.0
                                  ? elapsedMs : 0.0;
    const float releaseDb = static_cast<float> (
        boundedElapsedMs * (double) releaseDbPerSecond / 1000.0);
    if (averageMs > 0.0)
    {
        // An averaged line (AVG, LOW) keeps its own rule. Only a chosen slower FALL adds a limit to
        // how fast it may fall; the established rate leaves the average exactly as it was.
        const auto before = displayed;
        advanceAverageCurve (displayed, target, elapsedMs, averageMs);
        if (releaseDbPerSecond < absoluteReleaseDb * 1000.0f / (float) absoluteReleaseDurationMs)
            for (std::size_t index = 0; index < Size; ++index)
                if (std::isfinite (before[index]) && displayed[index] < before[index] - releaseDb)
                    displayed[index] = before[index] - releaseDb;
        return;
    }
    for (std::size_t index = 0; index < Size; ++index)
    {
        if (! std::isfinite (target[index]))
            continue;
        if (! std::isfinite (displayed[index])) displayed[index] = target[index];
        else if (target[index] >= displayed[index])
            displayed[index] += (target[index] - displayed[index]) * (smoothAttack
                ? static_cast<float> (-std::expm1 (-boundedElapsedMs / 30.0)) : 1.0f);
        else
            displayed[index] = std::max (target[index], displayed[index] - releaseDb);
    }
}

template <std::size_t Size>
void advanceSignedDeltaCurve (std::array<float, Size>& displayed,
                              const std::array<float, Size>& target,
                              double elapsedMs, double averageMs = 0.0) noexcept
{
    if (averageMs > 0.0) { advanceAverageCurve (displayed, target, elapsedMs, averageMs); return; }
    if (! std::isfinite (elapsedMs) || elapsedMs <= 0.0)
        return;
    const float response = static_cast<float> (
        -std::expm1 (-elapsedMs / signedDeltaResponseMs));
    for (std::size_t index = 0; index < Size; ++index)
    {
        if (! std::isfinite (target[index]))
            continue;
        if (! std::isfinite (displayed[index]))
            displayed[index] = target[index];
        else
            displayed[index] += response * (target[index] - displayed[index]);
    }
}

template <std::size_t Size>
void advanceSignedDeltaCurve (std::array<float, Size>& displayed,
                              const std::array<float, Size>& target,
                              std::array<uint8_t, Size>& previousValid,
                              const std::array<uint8_t, Size>& targetValid,
                              double elapsedMs, double averageMs = 0.0) noexcept
{
    // Missing bands have no dB value. Seed each returning band with its first real observation.
    auto measured = target;
    for (std::size_t index = 0; index < Size; ++index)
    {
        if (targetValid[index] == 0u) measured[index] = 0.0f;
        if (targetValid[index] == 0u || previousValid[index] == 0u)
            displayed[index] = measured[index];
    }
    advanceSignedDeltaCurve (displayed, measured, elapsedMs, averageMs);
    previousValid = targetValid;
}
}
