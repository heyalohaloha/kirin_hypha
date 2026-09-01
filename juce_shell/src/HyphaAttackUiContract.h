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
    constexpr int headerHeight = 30;
    constexpr int axisLabelHeight = 18;
    constexpr int detailMetricsHeight = 120;
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
    constexpr float organismFeather = 0.12f;
    constexpr float strengthCoreRadius = 0.34f;
    constexpr float textureBodyRadius = 0.62f;
    constexpr float brightnessShellRadius = 0.84f;
    constexpr float brightnessShellHalfWidth = 0.040f;
    constexpr float transientAuraRadius = 1.03f;
    constexpr float transientAuraHalfWidth = 0.050f;
    constexpr float transientAuraReach = 7.0f;
    // A colour-vision-resilient gold/cyan family. Fixed radial bands retain each observation's
    // chroma; magnitude is carried by lightness, opacity and thickness, never by hue movement.
    constexpr std::uint32_t waveformColour = 0xff789db7;
    constexpr std::uint32_t strengthColour = 0xffefc977;
    constexpr std::uint32_t brightnessColour = 0xffa9dcf3;
    constexpr std::uint32_t transientColour = 0xff59d6d0;
    constexpr std::uint32_t textureColour = 0xffdd8b54;
    constexpr std::uint32_t selectionColour = 0xffffe6ad;

    constexpr int rgbChromaRange (std::uint32_t colour) noexcept
    {
        const auto red = static_cast<int> ((colour >> 16) & 0xff);
        const auto green = static_cast<int> ((colour >> 8) & 0xff);
        const auto blue = static_cast<int> (colour & 0xff);
        const auto maximum = red > green ? (red > blue ? red : blue)
                                         : (green > blue ? green : blue);
        const auto minimum = red < green ? (red < blue ? red : blue)
                                         : (green < blue ? green : blue);
        return maximum - minimum;
    }

    static_assert (rgbChromaRange (waveformColour) >= 56);
    static_assert (rgbChromaRange (strengthColour) >= 56);
    static_assert (rgbChromaRange (brightnessColour) >= 56);
    static_assert (rgbChromaRange (transientColour) >= 56);
    static_assert (rgbChromaRange (textureColour) >= 56);

    static_assert (strengthCoreRadius < textureBodyRadius);
    static_assert (textureBodyRadius < brightnessShellRadius
                       - brightnessShellHalfWidth * (1.0f + organismFeather));
    static_assert (brightnessShellRadius
                       + brightnessShellHalfWidth * (1.0f + organismFeather)
                   < transientAuraRadius
                       - transientAuraHalfWidth * (1.0f + organismFeather));

    constexpr int metricsHeight (int totalHeight) noexcept
    {
        return totalHeight >= 250 ? detailMetricsHeight
             : totalHeight >= 190 ? 86
             : totalHeight >= 145 ? 62 : 0;
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
