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

    constexpr int margin            = 10;
    constexpr int topSpace          = 7;
    constexpr int titleHeight       = 27;
    constexpr int titleWidth        = 42;
    constexpr int ledSize           = 12;
    constexpr int pairStatusWidth   = 50;
    constexpr int nameFieldHeight   = 24;
    constexpr int pairDropdownWidth = 28;
    constexpr int metricRowHeight   = 25;
    constexpr int metricRowPitch    = 27;
    constexpr int metricColumnGap   = 4;
    constexpr int metricHeight      = metricRowPitch * 2 + metricRowHeight;
    constexpr int postControlHeight = 28;
    constexpr int feedbackHeight    = 20;
    constexpr int preDisplayLineHeight = 18;

    // PopupMenu is a separate native window in desktop AU/VST3 hosts. Its geometry therefore
    // cannot inherit the 300x200 editor scale and must be explicit in the shared contract.
    constexpr int pairMenuItemHeight     = 28;
    constexpr int pairMenuMinimumWidth   = editorWidth;
    constexpr int pairMenuMaximumColumns = 1;

    constexpr const char* labelFontFamily = ".SF NS";
    constexpr const char* monoFontFamily  = ".SF NS Mono";

    constexpr float titleFontHeight       = 20.0f;
    constexpr float pairStatusFontHeight  = 13.0f;
    constexpr float feedbackFontHeight    = 13.0f;
    constexpr float preDisplayPrimaryFontHeight = 12.0f;
    constexpr float preDisplayDetailFontHeight = 11.0f;
    constexpr float metricLabelFontHeight = 12.0f;
    constexpr float metricValueFontHeight = 17.0f;
    constexpr float metricUnitFontHeight  = 12.0f;
    constexpr float nameFontHeight        = 16.0f;
    constexpr float menuFontHeight        = 16.0f;
    constexpr float framedButtonFontHeight   = 15.0f;
    constexpr float framelessButtonFontHeight = 13.0f;
    constexpr float metricMinimumLabelWidth = 40.0f;
    constexpr float metricHorizontalSpacing = 4.0f;
    constexpr int loudnessSelectorWidth = 40;

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
        Rect preDisplayPrimary;
        Rect preDisplayDetail;
        Rect feedback;
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
            const int nameY = topSpace + titleHeight + 2;
            layout.name = { margin,
                            nameY,
                            width - 2 * margin - pairDropdownWidth - 4,
                            nameFieldHeight };
            layout.pairDropdown = { width - margin - pairDropdownWidth,
                                    nameY,
                                    pairDropdownWidth,
                                    nameFieldHeight };
            layout.floraY = nameY + nameFieldHeight + 3;
            layout.metricTop = layout.floraY + 1 + 3;
            const int afterMetric = layout.metricTop + metricHeight;
            layout.postControls = { margin,
                                    afterMetric + 3,
                                    width - 2 * margin,
                                    postControlHeight };
        }
        else
        {
            const int fieldLeft = margin + titleWidth + 4;
            const int fieldRight = layout.pairStatus.x - 6;
            layout.name = { fieldLeft, topSpace, fieldRight - fieldLeft, titleHeight };
            layout.floraY = topSpace + titleHeight + 4;
            layout.metricTop = layout.floraY + 1 + 4;
            const int displayTop = layout.metricTop + metricHeight + 4;
            layout.preDisplayPrimary = { margin, displayTop,
                                         width - 2 * margin, preDisplayLineHeight };
            layout.preDisplayDetail = { margin, displayTop + preDisplayLineHeight,
                                        width - 2 * margin, preDisplayLineHeight };
        }

        // One bottom-aligned feedback row is shared by both roles. Transient user feedback,
        // persistent I/O errors, and the short Keeping acknowledgement never overlap each other.
        layout.feedback = { margin,
                            editorHeight - feedbackHeight - 2,
                            width - 2 * margin,
                            feedbackHeight };
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

    constexpr Rect loudnessSelectorBounds (int metricTop, int width = editorWidth) noexcept
    {
        const auto first = metricCellBounds (0, metricTop, width);
        return { first.x, first.y, loudnessSelectorWidth, first.height };
    }

    enum class Metric
    {
        lufs,
        truePeak,
        maxTruePeak,
        crest,
        psr,
        integrated,
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
            case Metric::lufs:      return { "", "", "LUFS", "LU" };
            case Metric::truePeak:  return { "TP", "TP", "dBTP", "dB" };
            case Metric::maxTruePeak: return { "Max TP", "Max TP", "dBTP", "dBTP" };
            case Metric::crest:     return { "Crest", "Crest", "dB", "dB" };
            case Metric::psr:       return { "PSR", "PSR", "dB", "dB" };
            case Metric::integrated:return { "I", "I", "LUFS", "LUFS" };
            case Metric::sharpness: return { "Sharp", "Sharp", "acum", "acum" };
        }
        return { "", "", "", "" };
    }

    struct MetricSlot
    {
        Metric metric;
        bool maximum;
        bool deltaEligible;
    };

    // Watch is always a 2x3 current/MAX grid. Record is always a 2x3 six-metric grid.
    constexpr std::array<MetricSlot, 6> watchMetrics {{
        { Metric::lufs, false, true }, { Metric::lufs, true, false },
        { Metric::truePeak, false, true }, { Metric::truePeak, true, false },
        { Metric::crest, false, true }, { Metric::crest, true, false },
    }};

    constexpr std::array<MetricSlot, 6> recordMetrics {{
        { Metric::lufs, false, true }, { Metric::psr, false, true },
        { Metric::maxTruePeak, false, false }, { Metric::integrated, false, false },
        { Metric::crest, false, true }, { Metric::sharpness, false, true },
    }};

    static_assert (metricValueFontHeight >= 17.0f
                       && metricLabelFontHeight >= 12.0f
                       && metricUnitFontHeight >= 12.0f
                       && nameFontHeight >= 16.0f
                       && pairStatusFontHeight >= 13.0f,
                   "The 300x200 editor must retain the legibility floor agreed for release");
    static_assert (menuFontHeight >= 16.0f && pairMenuItemHeight >= 28
                       && pairMenuMinimumWidth >= editorWidth && pairMenuMaximumColumns == 1,
                   "The pair menu must remain readable and single-column in every plugin format");
    static_assert (bottom (editorLayout (true).feedback) <= editorHeight,
                   "POST feedback row must fit the 300x200 editor boundary");
    static_assert (bottom (metricCellBounds (5, editorLayout (true).metricTop))
                       < editorLayout (true).postControls.y,
                   "POST metrics and controls must not overlap");
    static_assert (bottom (editorLayout (true).postControls) <= editorLayout (true).feedback.y,
                   "POST controls and feedback must not overlap");
    static_assert (bottom (metricCellBounds (5, editorLayout (true).metricTop)) <= editorHeight,
                   "POST metric grid must fit the editor");
    static_assert (bottom (metricCellBounds (5, editorLayout (false).metricTop)) <= editorHeight,
                   "PRE metric grid must fit the editor");
    static_assert (bottom (metricCellBounds (5, editorLayout (false).metricTop))
                       < editorLayout (false).preDisplayPrimary.y,
                   "PRE metric grid and guide display must not overlap");
    static_assert (bottom (editorLayout (false).preDisplayDetail)
                       <= editorLayout (false).feedback.y,
                   "PRE two-line guide and feedback must not overlap");
    static_assert (editorLayout (true).preDisplayPrimary.width == 0
                       && editorLayout (true).preDisplayDetail.width == 0,
                   "POST must not acquire PRE display geometry");
    static_assert (editorLayout (false).name.width >= 160,
                   "Every valid 16-character PRE name must fit at the release font size");
    static_assert (watchMetrics[1].maximum && watchMetrics[3].maximum && watchMetrics[5].maximum,
                   "Watch right column must remain MAX for all three metrics");
    static_assert (! recordMetrics[2].deltaEligible && ! recordMetrics[3].deltaEligible,
                   "Record Max TP and I are absolute session values in PRE and POST");
}
