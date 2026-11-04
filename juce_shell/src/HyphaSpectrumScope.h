#pragma once

#include <cmath>
#include <cstdint>

// FULL and LOW as the display checks them, with the numbers of SpectrumLayout::for_scope. Every
// Spectrum path (POST-PRE, POST alone, M/S) uses this one definition, so LOW is never refused by
// one of them while another shows it.
namespace hypha::spectrum_scope
{
inline constexpr std::uint32_t fullWindowAt48k = 4'096u;
inline constexpr std::uint32_t lowWindowAt48k = 16'384u;
inline constexpr float lowMinimumHz = 20.0f;
inline constexpr float lowMaximumHz = 300.0f;

constexpr std::uint32_t apertureFor (std::uint32_t sampleRate, std::uint32_t windowAt48k) noexcept
{
    return static_cast<std::uint32_t> (
        (static_cast<std::uint64_t> (sampleRate) * windowAt48k + 24'000u) / 48'000u);
}

constexpr std::uint32_t fftFor (std::uint32_t aperture) noexcept
{
    std::uint32_t fft = 1u;
    while (fft < aperture * 2u)
        fft <<= 1u;
    return fft;
}

inline bool matchesWindow (std::uint32_t sampleRate, std::uint32_t aperture, std::uint32_t fft,
                           float approximateBelowHz, std::uint32_t windowAt48k) noexcept
{
    const auto expected = apertureFor (sampleRate, windowAt48k);
    return expected > 0u && aperture == expected && fft == fftFor (expected)
        && std::isfinite (approximateBelowHz)
        && std::abs (approximateBelowHz
                     - 3.0f * static_cast<float> (sampleRate) / static_cast<float> (expected))
               < 0.001f;
}

inline bool validLayout (std::uint32_t sampleRate, std::uint32_t aperture, std::uint32_t fft,
                         float approximateBelowHz) noexcept
{
    return matchesWindow (sampleRate, aperture, fft, approximateBelowHz, fullWindowAt48k)
        || matchesWindow (sampleRate, aperture, fft, approximateBelowHz, lowWindowAt48k);
}
}
