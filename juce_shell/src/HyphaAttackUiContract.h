#pragma once

#include <array>
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
    constexpr int modeControlMinimumWidth = 112;
    constexpr int statusControlMinimumWidth = 84;
    constexpr std::size_t laneCount = 4;
    constexpr int historyMinimumHeight = 36;
    constexpr int historyLineMinimumHeight = 24;
    constexpr int laneMinimumHeight = 18;
    constexpr int laneMaximumHeight = 52;
    // Lanes receive this share of the body; HISTORY keeps the rest so it stays the largest area.
    constexpr int laneShareNumerator = 13;
    constexpr int laneShareDenominator = 100;

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

    constexpr int axisRowHeight (const presentation::Context& context) noexcept
    {
        const auto required = roundPixels (typography::resolve (
            context, typography::TextRole::axis,
            typography::Composition::visualization).lineHeight) + 1;
        return required > axisLabelHeight ? required : axisLabelHeight;
    }

    constexpr int readoutLineHeight (const presentation::Context& context) noexcept
    {
        const auto required = roundPixels (typography::resolve (
            context, typography::TextRole::readout,
            typography::Composition::visualization).lineHeight) + 4;
        return required > 16 ? required : 16;
    }

    constexpr float absoluteFloorDb = -72.0f;
    // A colour-vision-resilient gold/cyan family. Labels, lane order and bar direction remain the
    // primary identifiers; magnitude and sign never depend on hue alone.
    constexpr std::uint32_t waveformColour = 0xff32ced7;
    constexpr std::uint32_t strengthColour = 0xffefc977;
    constexpr std::uint32_t sharpnessColour = 0xffa9dcf3;
    constexpr std::uint32_t transientColour = 0xff59d6d0;
    constexpr std::uint32_t crestColour = 0xffdd8b54;
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
    static_assert (rgbChromaRange (crestColour) >= 56);

    struct Box
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
        constexpr int right() const noexcept { return x + width; }
        constexpr int bottom() const noexcept { return y + height; }
    };

    enum class Arrangement
    {
        header,  // too small for any observation surface
        line,    // selected-hit values in one row, with HISTORY above when it fits
        lanes,   // HISTORY above four per-hit lanes sharing its time axis
    };

    struct Layout
    {
        Arrangement arrangement = Arrangement::header;
        Box header, history, axis, line;
        std::array<Box, laneCount> lanes {};
        int labelWidth = 0;
        int readoutWidth = 0;
        bool loupe = false;
    };

    constexpr int labelColumnWidth (observatory::Density density) noexcept
    {
        switch (density)
        {
            case observatory::Density::compact:     return 56;
            case observatory::Density::focused:     return 60;
            case observatory::Density::standard:    return 72;
            case observatory::Density::observatory: return 88;
            case observatory::Density::inspection:  return 108;
        }
        return 56;
    }

    constexpr int readoutColumnWidth (observatory::Density density) noexcept
    {
        switch (density)
        {
            case observatory::Density::compact:     return 84;
            case observatory::Density::focused:     return 92;
            case observatory::Density::standard:    return 104;
            case observatory::Density::observatory: return 132;
            case observatory::Density::inspection:  return 236;
        }
        return 84;
    }

    constexpr Layout layoutFor (int width, int height,
                                const presentation::Context& context) noexcept
    {
        Layout layout;
        if (width <= 0 || height <= 0)
            return layout;
        const auto headerRows = headerHeightFor (context);
        const auto header = headerRows < height ? headerRows : height;
        layout.header = { 0, 0, width, header };
        const auto body = height - header;
        const auto axis = axisRowHeight (context);
        const auto line = readoutLineHeight (context);
        if (body >= historyMinimumHeight + axis + static_cast<int> (laneCount) * laneMinimumHeight)
        {
            auto lane = (body * laneShareNumerator + laneShareDenominator / 2) / laneShareDenominator;
            lane = lane < laneMinimumHeight ? laneMinimumHeight
                 : lane > laneMaximumHeight ? laneMaximumHeight : lane;
            const auto history = body - axis - static_cast<int> (laneCount) * lane;
            layout.arrangement = Arrangement::lanes;
            layout.history = { 0, header, width, history };
            layout.axis = { 0, header + history, width, axis };
            for (std::size_t index = 0; index < laneCount; ++index)
                layout.lanes[index] = { 0, header + history + axis + static_cast<int> (index) * lane,
                                        width, lane };
            const auto label = labelColumnWidth (context.density);
            const auto readout = readoutColumnWidth (context.density);
            layout.labelWidth = label < width / 6 ? label : width / 6;
            layout.readoutWidth = readout < width / 4 ? readout : width / 4;
            layout.loupe = context.density == observatory::Density::inspection
                        && layout.readoutWidth >= 160 && history >= 90;
            return layout;
        }
        if (body < line)
            return layout;
        layout.arrangement = Arrangement::line;
        const auto history = body - axis - line;
        if (history >= historyLineMinimumHeight)
        {
            layout.history = { 0, header, width, history };
            layout.axis = { 0, header + history, width, axis };
            layout.line = { 0, header + history + axis, width, line };
        }
        else
        {
            layout.line = { 0, header, width, line };
        }
        return layout;
    }

    // The five editor bodies (POST, Guide absent, TIME navigation removed) keep the approved
    // arrangement: one selected-hit row at 100% and 125%, lanes from 150%, a loupe only at 300%.
    static_assert (layoutFor (292, 94, presentation::forEditor (300, 200)).arrangement
                   == Arrangement::line);
    static_assert (! layoutFor (292, 94, presentation::forEditor (300, 200)).history.empty());
    static_assert (layoutFor (363, 128, presentation::forEditor (375, 250)).arrangement
                   == Arrangement::line);
    static_assert (layoutFor (434, 164, presentation::forEditor (450, 300)).arrangement
                   == Arrangement::lanes);
    static_assert (layoutFor (580, 248, presentation::forEditor (600, 400)).arrangement
                   == Arrangement::lanes);
    static_assert (! layoutFor (580, 248, presentation::forEditor (600, 400)).loupe);
    static_assert (layoutFor (872, 412, presentation::forEditor (900, 600)).loupe);
    static_assert (layoutFor (872, 412, presentation::forEditor (900, 600)).lanes[3].bottom()
                   == 412);

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
