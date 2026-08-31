#pragma once

#include <cstdint>
#include <limits>

// ATTACK product-trial presentation. It maps confirmed event samples onto a fixed six-second axis
// and never attempts to infer an instrument from a waveform or expose an editable threshold. The
// environment variable remains only as a direct-open validation shortcut.
namespace hypha::attack_ui
{
    constexpr int presentationSeconds = 6;
    constexpr int presentationHz = 10;
    constexpr const char* activationEnvironmentVariable = "KIRIN_HYPHA_INTERNAL_ATTACK";
    constexpr const char* activationValue = "1";
    constexpr int minimumPlotWidth = 1;
    constexpr int headerHeight = 32;
    constexpr int axisLabelHeight = 22;
    constexpr int detailMetricsHeight = 42;
    constexpr int modeControlMaximumWidth = 112;
    constexpr float absoluteFloorDb = -72.0f;
    constexpr float strengthGlowOnDbfs = -42.0f;
    constexpr float strengthGlowFullDbfs = -6.0f;
    constexpr float brightnessGlowOnAcum = 0.60f;
    constexpr float brightnessGlowFullAcum = 2.50f;
    constexpr float transientGlowOnDb = 3.0f;
    constexpr float transientGlowFullDb = 15.0f;
    constexpr float textureGlowOn = 0.10f;
    constexpr float textureGlowFull = 0.65f;
    constexpr float strengthDifferenceGlowOnDb = 0.50f;
    constexpr float strengthDifferenceGlowFullDb = 6.0f;
    constexpr float brightnessDifferenceGlowOnAcum = 0.05f;
    constexpr float brightnessDifferenceGlowFullAcum = 0.60f;
    constexpr float transientDifferenceGlowOnDb = 0.50f;
    constexpr float transientDifferenceGlowFullDb = 6.0f;
    constexpr float textureDifferenceGlowOn = 0.04f;
    constexpr float textureDifferenceGlowFull = 0.35f;
    constexpr int featureTintRadiusMs = 120;
    // A low-chroma, colour-vision-resilient family. Magnitude is carried by lightness, opacity,
    // thickness and the fixed spatial grammar rather than by shifting hue between PRE and POST.
    constexpr std::uint32_t waveformColour = 0xff7893a3;
    constexpr std::uint32_t strengthColour = 0xffd6ad73;
    constexpr std::uint32_t brightnessColour = 0xff8dc9dc;
    constexpr std::uint32_t transientColour = 0xff88baaa;
    constexpr std::uint32_t textureColour = 0xffbd837c;
    constexpr std::uint32_t selectionColour = 0xffe7ddc6;

    constexpr int metricsHeight (int totalHeight) noexcept
    {
        return totalHeight < 120 ? 0
             : totalHeight >= 220 ? detailMetricsHeight
             : totalHeight >= 140 ? 32 : 20;
    }

    constexpr int modeControlWidth (int totalWidth) noexcept
    {
        return totalWidth >= modeControlMaximumWidth * 2
            ? modeControlMaximumWidth : totalWidth / 2;
    }

    constexpr int timelineHeight (int totalHeight) noexcept
    {
        const int available = totalHeight - headerHeight - axisLabelHeight
                            - metricsHeight (totalHeight);
        return available > 0 ? available : 0;
    }

    constexpr std::int64_t windowSamples (std::uint32_t sampleRate) noexcept
    {
        return static_cast<std::int64_t> (sampleRate) * presentationSeconds;
    }

    constexpr bool validTimeline (std::int64_t latestSample,
                                  std::uint32_t sampleRate) noexcept
    {
        return sampleRate > 0
            && latestSample >= std::numeric_limits<std::int64_t>::min()
                             + windowSamples (sampleRate);
    }

    constexpr bool eventIsVisible (std::int64_t eventSample,
                                   std::int64_t latestSample,
                                   std::uint32_t sampleRate) noexcept
    {
        if (! validTimeline (latestSample, sampleRate))
            return false;
        const auto first = latestSample - windowSamples (sampleRate);
        return eventSample >= first && eventSample <= latestSample;
    }

    constexpr int eventX (std::int64_t eventSample,
                          std::int64_t latestSample,
                          std::uint32_t sampleRate,
                          int plotWidth) noexcept
    {
        if (! eventIsVisible (eventSample, latestSample, sampleRate)
            || plotWidth < minimumPlotWidth)
            return -1;
        const auto span = windowSamples (sampleRate);
        const auto offset = eventSample - (latestSample - span);
        return static_cast<int> ((static_cast<long double> (offset) * (plotWidth - 1))
                                 / static_cast<long double> (span));
    }

    constexpr int sampleX (std::int64_t sample,
                           std::int64_t firstSample,
                           std::int64_t lastSample,
                           int plotWidth) noexcept
    {
        if (plotWidth < minimumPlotWidth || lastSample <= firstSample
            || sample < firstSample || sample > lastSample)
            return -1;
        return static_cast<int> ((static_cast<long double> (sample - firstSample)
                                  * (plotWidth - 1))
                                 / static_cast<long double> (lastSample - firstSample));
    }
}
