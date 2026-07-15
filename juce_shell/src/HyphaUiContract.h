#pragma once

#include <array>
#include <cstdint>

// Pure C++ release UI contract. This header deliberately has no JUCE dependency so the exact
// geometry, typography, palette, and metric inventory can be validated before building either
// plugin format. AU and VST3 both compile the same JUCE editor, which consumes this contract.
namespace hypha::ui_contract
{
    constexpr int editorWidth  = 300;
    constexpr int editorHeight = 200;

    constexpr int margin          = 10;
    constexpr int topSpace        = 8;
    constexpr int titleHeight     = 26;
    constexpr int titleWidth      = 58;
    constexpr int ledSize         = 12;
    constexpr int pairStatusWidth = 52;
    constexpr int metricHeight    = 70;
    constexpr int metricRowHeight = 22;
    constexpr int metricRowPitch  = 24;
    constexpr int metricColumnGap = 6;
    constexpr int postControlHeight = 26;

    constexpr const char* labelFontFamily = ".SF NS";
    constexpr const char* monoFontFamily  = ".SF NS Mono";

    constexpr float titleFontHeight       = 20.0f;
    constexpr float pairStatusFontHeight  = 11.0f;
    constexpr float bannerFontHeight      = 13.0f;
    constexpr float statusFontHeight      = 11.0f;
    constexpr float metricLabelFontHeight = 11.0f;
    constexpr float metricValueFontHeight = 14.0f;
    constexpr float metricUnitFontHeight  = 10.0f;
    constexpr float nameFontHeight        = 14.0f;
    constexpr float menuFontHeight        = 12.0f;
    constexpr float framedButtonFontHeight   = 15.0f;
    constexpr float framelessButtonFontHeight = 13.0f;
    constexpr float metricMinimumLabelWidth = 40.0f;

    constexpr const char* preTitle = "PRE";
    constexpr const char* postTitle = "POST";
    constexpr const char* maximumLabel = "MAX";
    constexpr const char* keepLabel = "Keep";
    constexpr const char* stopLabel = "Stop";

    constexpr std::uint32_t background = 0xff0d0f1a;
    constexpr std::uint32_t normal     = 0xffe0e0e0;
    constexpr std::uint32_t muted      = 0xff606060;
    constexpr std::uint32_t flora      = 0xffd4a043;
    constexpr std::uint32_t floraBright = 0xffffe0a0;
    constexpr std::uint32_t ledBlue    = 0xff4488cc;
    constexpr std::uint32_t ledGreen   = 0xff4cc07a;
    constexpr std::uint32_t ledYellow  = 0xffccaa44;
    constexpr std::uint32_t ledGrey    = 0xff555558;

    struct Rect
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    constexpr int right (Rect r) noexcept  { return r.x + r.width; }
    constexpr int bottom (Rect r) noexcept { return r.y + r.height; }

    struct EditorLayout
    {
        Rect title;
        Rect led;
        Rect pairStatus;
        Rect name;
        Rect pairDropdown;
        Rect postControls;
        Rect banner;
        Rect recordError;
        int floraY = 0;
        int metricTop = 0;
    };

    constexpr EditorLayout editorLayout (bool post, int width = editorWidth) noexcept
    {
        EditorLayout layout {};
        layout.title = { margin, topSpace, titleWidth, titleHeight };
        layout.led = { width - margin - ledSize,
                       topSpace + (titleHeight - ledSize) / 2,
                       ledSize,
                       ledSize };
        layout.pairStatus = { width - margin - ledSize - 6 - pairStatusWidth,
                              topSpace,
                              pairStatusWidth,
                              titleHeight };

        if (post)
        {
            constexpr int dropdownWidth = 22;
            const int nameY = topSpace + titleHeight + 4;
            layout.name = { margin, nameY, width - 2 * margin - dropdownWidth - 4, 22 };
            layout.pairDropdown = { width - margin - dropdownWidth, nameY, dropdownWidth, 22 };
            layout.floraY = nameY + 22 + 4;
            layout.metricTop = layout.floraY + 1 + 4;
            const int afterMetric = layout.metricTop + metricHeight;
            layout.postControls = { margin, afterMetric + 4, width - 2 * margin, postControlHeight };
            layout.banner = { margin,
                              afterMetric + 4 + postControlHeight + 1,
                              width - 2 * margin,
                              16 };
        }
        else
        {
            const int fieldLeft = margin + titleWidth + 6;
            const int fieldRight = layout.pairStatus.x - 6;
            layout.name = { fieldLeft, topSpace, fieldRight - fieldLeft, titleHeight };
            layout.floraY = topSpace + titleHeight + 4;
            layout.metricTop = layout.floraY + 1 + 6;
            layout.banner = { margin,
                              layout.metricTop + metricHeight + 4,
                              width - 2 * margin,
                              16 };
        }

        layout.recordError = { margin,
                               layout.banner.y + 16,
                               width - 2 * margin,
                               14 };
        return layout;
    }

    constexpr Rect metricCellBounds (int index, int metricTop, int width = editorWidth) noexcept
    {
        const int areaWidth = width - 2 * margin;
        const int cellWidth = (areaWidth - metricColumnGap) / 2;
        const int row = index / 2;
        const int column = index % 2;
        return { margin + column * (cellWidth + metricColumnGap),
                 metricTop + row * metricRowPitch,
                 cellWidth,
                 metricRowHeight };
    }

    enum class Metric
    {
        lufs,
        truePeak,
        crest,
        psr,
        loudness,
        sharpness,
    };

    struct MetricText
    {
        const char* absoluteLabel;
        const char* deltaSuffix;
        const char* absoluteUnit;
        const char* deltaUnit;
    };

    constexpr MetricText metricText (Metric metric) noexcept
    {
        switch (metric)
        {
            case Metric::lufs:      return { "LUFS-M", "LUFS", "LUFS", "LU" };
            case Metric::truePeak:  return { "TP", "TP", "dBTP", "dB" };
            case Metric::crest:     return { "Crest", "Crest", "dB", "dB" };
            case Metric::psr:       return { "PSR", "PSR", "dB", "dB" };
            case Metric::loudness:  return { "N", "N", "sone", "sone" };
            case Metric::sharpness: return { "Sharp", "Sharp", "acum", "acum" };
        }
        return { "", "", "", "" };
    }

    struct MetricSlot
    {
        Metric metric;
        bool maximum;
    };

    // Watch is always a 2x3 current/MAX grid. Record is always a 2x3 six-metric grid.
    constexpr std::array<MetricSlot, 6> watchMetrics {{
        { Metric::lufs, false }, { Metric::lufs, true },
        { Metric::truePeak, false }, { Metric::truePeak, true },
        { Metric::crest, false }, { Metric::crest, true },
    }};

    constexpr std::array<MetricSlot, 6> recordMetrics {{
        { Metric::lufs, false }, { Metric::psr, false },
        { Metric::truePeak, false }, { Metric::loudness, false },
        { Metric::crest, false }, { Metric::sharpness, false },
    }};

    static_assert (bottom (editorLayout (true).recordError) == editorHeight,
                   "POST status row must end exactly at the 300x200 editor boundary");
    static_assert (bottom (metricCellBounds (5, editorLayout (true).metricTop)) <= editorHeight,
                   "POST metric grid must fit the editor");
    static_assert (bottom (metricCellBounds (5, editorLayout (false).metricTop)) <= editorHeight,
                   "PRE metric grid must fit the editor");
    static_assert (watchMetrics[1].maximum && watchMetrics[3].maximum && watchMetrics[5].maximum,
                   "Watch right column must remain MAX for all three metrics");
}
