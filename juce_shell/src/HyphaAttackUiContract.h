#pragma once

#include <cstdint>
#include <limits>

#include "HyphaTypographyContract.h"

// ATTACK product presentation. It maps confirmed event samples onto a fixed six-second axis
// and never attempts to infer an instrument from a waveform or expose an editable threshold. The
// environment variable remains a direct-open shortcut within TRACK/STEM; it never changes the
// saved meter context to bypass the DRUM admission rule.
namespace hypha::attack_ui
{
    constexpr int presentationSeconds = 6;
    constexpr int presentationHz = 10;
    constexpr const char* activationEnvironmentVariable = "KIRIN_HYPHA_OPEN_ATTACK";
    constexpr const char* activationValue = "1";
    constexpr int minimumPlotWidth = 1;
    constexpr int headerHeight = 28;
    constexpr int axisLabelHeight = 14;
    constexpr int transientRowMaximumHeight = 44;
    constexpr int modeControlMinimumWidth = 112;
    constexpr int statusControlMinimumWidth = 84;

    constexpr int ceilPixels (float value) noexcept
    {
        const auto whole = static_cast<int> (value);
        return whole + (value > static_cast<float> (whole) ? 1 : 0);
    }

    constexpr int roundPixels (float value) noexcept
    {
        return static_cast<int> (value + 0.5f);
    }

    constexpr int titleRowHeight (const presentation::Context& context) noexcept
    {
        return roundPixels (typography::resolve (
            context, typography::TextRole::sectionTitle,
            typography::Composition::visualization).lineHeight);
    }

    constexpr int statusRowHeight (const presentation::Context& context) noexcept
    {
        const auto legend = typography::resolve (
            context, typography::TextRole::legend,
            typography::Composition::visualization).lineHeight;
        const auto status = typography::resolve (
            context, typography::TextRole::status,
            typography::Composition::visualization).lineHeight;
        return roundPixels (legend > status ? legend : status);
    }

    constexpr int headerHeightFor (const presentation::Context& context) noexcept
    {
        const auto required = titleRowHeight (context) + statusRowHeight (context);
        return required > headerHeight ? required : headerHeight;
    }
    constexpr float absoluteFloorDb = -72.0f;
    constexpr float strengthGlowOnDbfs = -42.0f;
    constexpr float strengthGlowFullDbfs = -6.0f;
    constexpr float sharpnessGlowOnAcum = 0.60f;
    constexpr float sharpnessGlowFullAcum = 2.50f;
    constexpr float transientGlowOnDb = 3.0f;
    constexpr float transientGlowFullDb = 15.0f;
    constexpr float textureGlowOn = 0.10f;
    constexpr float textureGlowFull = 0.65f;
    // A colour-vision-resilient gold/cyan family. Labels and geometry remain the primary
    // identifiers; magnitude never depends on hue movement alone.
    constexpr std::uint32_t waveformColour = 0xff32ced7;
    constexpr std::uint32_t strengthColour = 0xffefc977;
    constexpr std::uint32_t sharpnessColour = 0xffa9dcf3;
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
    static_assert (rgbChromaRange (sharpnessColour) >= 56);
    static_assert (rgbChromaRange (transientColour) >= 56);
    static_assert (rgbChromaRange (textureColour) >= 56);

    constexpr int timelineHeight (int totalHeight) noexcept
    {
        return totalHeight >= 400 ? 64
             : totalHeight >= 260 ? 42
             : totalHeight >= 170 ? 36 : 0;
    }

    constexpr int transientHeight (int totalHeight) noexcept
    {
        return totalHeight >= 400 ? transientRowMaximumHeight
             : totalHeight >= 260 ? 30
             : totalHeight >= 170 ? 26
             : totalHeight >= 115 ? 24 : 0;
    }

    constexpr int axisHeight (int totalHeight) noexcept
    {
        return timelineHeight (totalHeight) > 0 ? axisLabelHeight : 0;
    }

    constexpr int metricsHeight (int totalHeight) noexcept
    {
        const int available = totalHeight - headerHeight - axisHeight (totalHeight)
                            - timelineHeight (totalHeight) - transientHeight (totalHeight);
        return available > 0 ? available : 0;
    }

    constexpr int metricsHeightFor (int totalHeight,
                                    const presentation::Context& context) noexcept
    {
        const int available = totalHeight - headerHeightFor (context)
                            - axisHeight (totalHeight) - timelineHeight (totalHeight)
                            - transientHeight (totalHeight);
        return available > 0 ? available : 0;
    }

    constexpr int modeControlWidth (int totalWidth,
                                    int requiredWidth = modeControlMinimumWidth) noexcept
    {
        const auto desired = requiredWidth > modeControlMinimumWidth
            ? requiredWidth : modeControlMinimumWidth;
        return desired < totalWidth / 2 ? desired : totalWidth / 2;
    }

    constexpr int statusControlWidth (int totalWidth, int requiredWidth) noexcept
    {
        const auto desired = requiredWidth > statusControlMinimumWidth
            ? requiredWidth : statusControlMinimumWidth;
        return desired < totalWidth / 3 ? desired : totalWidth / 3;
    }

    constexpr const char* transientTitle (const presentation::Context& context) noexcept
    {
        return context.density == observatory::Density::compact ? "TR dB"
             : context.density == observatory::Density::focused ? "TRANS dB"
                                                                : "TRANSIENT dB";
    }

    constexpr int transientTitleWidth (int totalWidth, int requiredWidth) noexcept
    {
        const auto maximum = totalWidth / 4;
        return requiredWidth < maximum ? requiredWidth : maximum;
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
