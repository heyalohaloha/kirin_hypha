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
    // Event windows in about-1 ms content bins, kept equal to kirin_measure's ATTACK_HEAD_BINS,
    // ATTACK_BODY_BINS and ATTACK_SHAPE_LEAD_BINS by a kirin_hypha_ffi test: the 30 ms head, the
    // body of up to 100 ms that ends early at the next onset, and 20 ms of shape before the head.
    constexpr std::int64_t headBins = 30;
    constexpr std::int64_t bodyBins = 100;
    constexpr std::int64_t shapeLeadBins = 20;
    // A colour-vision-resilient gold/cyan family. Labels, lane order and bar direction remain the
    // primary identifiers; magnitude and sign never depend on hue alone.
    constexpr std::uint32_t waveformColour = 0xffe0bd7e;
    constexpr std::uint32_t strengthColour = 0xffd9a24e;
    constexpr std::uint32_t sharpnessColour = 0xffb3a2e6; // ui_contract::sharpness
    constexpr std::uint32_t transientColour = 0xff7fcfd8;
    constexpr std::uint32_t crestColour = 0xffd0835a;
    // A pale ice cyan: apart from ivory text and glints and from the TRANSIENT cyan.
    constexpr std::uint32_t selectionColour = 0xffb5e6ef;
    // PRE is the reference: a warm light neutral that stays legible over the gold POST body.
    constexpr std::uint32_t preTraceColour = 0xffd8d0c4;

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
        // The compact meter sizes read DRUM as one line of four differences, even though their
        // body (with the footer folded into the header) would now hold four short lanes.
        const bool compactMeter = context.density == observatory::Density::compact
                               || context.density == observatory::Density::focused;
        if (! compactMeter
            && body >= historyMinimumHeight + axis + static_cast<int> (laneCount) * laneMinimumHeight)
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

    // Cells inside a layout. Painters, hit testing and tests all derive geometry from these, so
    // no caller repeats an inset. Rectangle arithmetic matches juce::Rectangle.
    constexpr Box reduced (Box box, int dx, int dy) noexcept
    {
        const auto width = box.width - 2 * dx;
        const auto height = box.height - 2 * dy;
        return { box.x + dx, box.y + dy, width > 0 ? width : 0, height > 0 ? height : 0 };
    }

    constexpr Box labelCell (const Layout& layout, Box row) noexcept
    {
        if (layout.arrangement != Arrangement::lanes)
            return {};
        return { row.x, row.y, layout.labelWidth < row.width ? layout.labelWidth : row.width,
                 row.height };
    }

    constexpr Box readoutCell (const Layout& layout, Box row) noexcept
    {
        if (layout.arrangement != Arrangement::lanes)
            return {};
        const auto width = layout.readoutWidth < row.width ? layout.readoutWidth : row.width;
        return { row.right() - width, row.y, width, row.height };
    }

    // HISTORY, the axis and every lane share one horizontal plot column, so a hit has the same x
    // in each of them.
    constexpr Box plotColumn (const Layout& layout, Box row) noexcept
    {
        if (layout.arrangement == Arrangement::lanes)
        {
            const auto label = labelCell (layout, row).width;
            row.x += label;
            row.width -= label;
            row.width -= layout.readoutWidth < row.width ? layout.readoutWidth : row.width;
        }
        return reduced (row, 1, 0);
    }

    constexpr Box historyPlot (const Layout& layout) noexcept
    {
        return layout.history.empty() ? Box {} : reduced (plotColumn (layout, layout.history), 0, 1);
    }

    constexpr Box axisPlot (const Layout& layout) noexcept
    {
        return layout.axis.empty() ? Box {} : plotColumn (layout, layout.axis);
    }

    constexpr Box lanePlot (const Layout& layout, std::size_t lane) noexcept
    {
        return layout.arrangement == Arrangement::lanes && lane < laneCount
            ? reduced (plotColumn (layout, layout.lanes[lane]), 0, 1) : Box {};
    }

    constexpr Box loupeArea (const Layout& layout) noexcept
    {
        return layout.loupe ? reduced (readoutCell (layout, layout.history), 2, 1) : Box {};
    }

    // "-6 s" sits at the left end of the axis plot and NOW at its right end.
    constexpr int axisLabelWidth (const Layout& layout) noexcept
    {
        const auto fifth = axisPlot (layout).width / 5;
        return fifth < 35 ? fifth : 35;
    }

    // Lane values stay inside the lane stage, so a cap never touches its edge.
    constexpr int laneInsetX = 1;
    constexpr int laneInsetY = 3;

    // The one-row readout: four equal cells (SHARPNESS takes the remainder), each led by a short
    // lane-colour accent before its text.
    constexpr int lineInset = 4;
    constexpr int lineAccentWidth = 5;

    constexpr Box lineCell (const Layout& layout, std::size_t lane) noexcept
    {
        const auto area = reduced (layout.line, lineInset, 0);
        const auto segment = area.width / static_cast<int> (laneCount);
        const auto x = area.x + segment * static_cast<int> (lane);
        return { x, area.y, lane + 1 == laneCount ? area.right() - x : segment, area.height };
    }

    constexpr bool sharesOnePlotColumn (const Layout& layout) noexcept
    {
        const auto history = historyPlot (layout);
        const auto axis = axisPlot (layout);
        if (history.empty() || axis.x != history.x || axis.width != history.width)
            return false;
        if (layout.arrangement == Arrangement::lanes)
            for (std::size_t lane = 0; lane < laneCount; ++lane)
                if (lanePlot (layout, lane).x != history.x
                    || lanePlot (layout, lane).width != history.width)
                    return false;
        return ! layout.loupe || loupeArea (layout).x >= history.right();
    }

    // The five editor bodies (POST, Guide absent, TIME navigation removed) keep the approved
    // arrangement: one selected-hit row at 100% and 125%, lanes from 150%, a loupe only at 300%.
    static_assert (sharesOnePlotColumn (layoutFor (292, 94, presentation::forEditor (300, 200))));
    static_assert (sharesOnePlotColumn (layoutFor (363, 128, presentation::forEditor (375, 250))));
    static_assert (sharesOnePlotColumn (layoutFor (434, 164, presentation::forEditor (450, 300))));
    static_assert (sharesOnePlotColumn (layoutFor (580, 248, presentation::forEditor (600, 400))));
    static_assert (sharesOnePlotColumn (layoutFor (872, 412, presentation::forEditor (900, 600))));
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
