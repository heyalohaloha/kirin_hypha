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
    constexpr int preTitleWidth     = 42;
    constexpr int titlePairGap      = 6;
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
    constexpr int preDisplayStateGap = 4;
    constexpr int preDisplayDetailMinimumWidth = 72;
    constexpr int preDisplayPresentationHz = 10;
    constexpr int spectrumPresentationHz = 30;
    // PopupMenu is a separate native window in desktop AU/VST3 hosts. Its geometry therefore
    // cannot inherit the 300x200 editor scale and must be explicit in the shared contract.
    constexpr int pairMenuItemHeight     = 28;
    constexpr int pairMenuMinimumWidth   = editorWidth;
    constexpr int pairMenuMaximumColumns = 1;
    // The release UI uses native platform fonts without redistributing either vendor's files.
    // Keep the established macOS appearance, but never ask Windows to resolve Apple's private
    // family names through the non-deterministic GDI fallback path.
    constexpr const char* labelFontFamily = ".SF NS";
    constexpr const char* monoFontFamily  = ".SF NS Mono";
    constexpr const char* windowsLabelFontFamily = "Segoe UI";
    constexpr const char* windowsMonoFontFamily  = "Consolas";
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
    constexpr int loudnessSelectorWidth             = 40;
    constexpr int loudnessDeltaMinimumPrefixWidth   = 8;
    constexpr int loudnessSelectorHorizontalInset   = 1;
    constexpr int loudnessSelectorVerticalInset     = 4;
    constexpr int loudnessSegmentMinimumWidth       = 12;
    constexpr int spectrumToggleWidth               = 84;
    constexpr int spectrumToggleHeight              = 21;
    constexpr int spectrumTitleGap                  = 8;
    constexpr int spectrumSizeToggleGap             = 6;
    constexpr int spectrumSizeToggleWidth           = 46;
    struct SpectrumSizePreset { int width, height; const char* buttonText; const char* tooltip; };
    constexpr std::array<SpectrumSizePreset, 3> spectrumSizePresets {{
        { 300, 200, "100%", "Spectrum size: 100%" },
        { 375, 250, "125%", "Spectrum size: 125%" },
        { 450, 300, "150%", "Spectrum size: 150%" },
    }};
    constexpr int spectrumPlotLeftInset              = 24;
    constexpr int spectrumPlotRightInset             = 25;
    constexpr int spectrumPlotTopInset               = 6;
    constexpr int spectrumPlotBottomInset            = 12;
    constexpr float spectrumLegendFontHeight         = 8.5f;
    constexpr int spectrumLegendTop                  = 1;
    constexpr int spectrumLegendHeight               = 10;
    constexpr int spectrumDeltaLegendLabelX          = 3;
    constexpr int spectrumDeltaLegendLabelWidth      = 12;
    constexpr int spectrumPreLegendSampleWidth       = 0;
    constexpr int spectrumPreLegendLabelX            = 19;
    constexpr int spectrumPreLegendLabelWidth        = 20;
    constexpr int spectrumPostLegendSampleWidth      = 0;
    constexpr int spectrumPostLegendLabelX           = 45;
    constexpr int spectrumPostLegendLabelWidth       = 28;
    constexpr float spectrumPreStrokeWidth           = 1.10f;
    constexpr float spectrumPreCurveAlpha            = 0.82f;
    constexpr float spectrumPostGlowStrokeWidth      = 2.60f;
    constexpr float spectrumPostGlowAlpha            = 0.07f;
    constexpr float spectrumPostStrokeWidth          = 1.10f;
    constexpr float spectrumPostCurveAlpha           = 0.74f;
    constexpr float spectrumDeltaLegendAlpha         = 0.98f;
    constexpr float spectrumPreLegendAlpha           = 0.92f;
    constexpr float spectrumPostLegendAlpha          = 0.90f;
    constexpr int spectrumHoverReadoutWidth          = 96;
    constexpr int spectrumHoverReadoutHeight         = 15;
    constexpr int spectrumHoverReadoutInset          = 2;
    constexpr int spectrumHoverFrequencyX            = 6;
    constexpr int spectrumHoverFrequencyWidth        = 52;
    constexpr int spectrumHoverDeltaX                = 58;
    constexpr int spectrumHoverDeltaWidth            = 34;
    constexpr float spectrumHoverReadoutRadius       = 4.5f;
    constexpr float spectrumHoverLineWidth           = 0.75f;
    constexpr std::array<float, 13> spectrumTipAlpha {
        0.0f, 0.025f, 0.05f, 0.08f, 0.115f, 0.15f, 0.19f,
        0.235f, 0.28f, 0.325f, 0.365f, 0.40f, 0.43f };

    constexpr const char* preTitle = "PRE";
    constexpr const char* postTitle = "POST";
    constexpr const char* maximumLabel = "MAX";
    constexpr const char* keepLabel = "Keep";
    constexpr const char* stopLabel = "Stop";
    constexpr const char* spectrumShowTooltip = "Show POST - PRE spectrum";
    constexpr const char* spectrumHideTooltip = "Return to meters";
    constexpr int spectrumTooltipMaximumCharacters = 24;

    constexpr const char* spectrumTooltip (bool spectrumMode) noexcept
    {
        return spectrumMode ? spectrumHideTooltip : spectrumShowTooltip;
    }

    constexpr std::uint32_t background = 0xff0d0f1a;
    constexpr std::uint32_t normal     = 0xffe0e0e0;
    constexpr std::uint32_t muted      = 0xff606060;
    constexpr std::uint32_t preDisplayContextDetail = 0xff898989;
    constexpr std::uint32_t flora      = 0xffd4a043;
    constexpr std::uint32_t floraBright = 0xffffe0a0;
    constexpr std::uint32_t spectrumDelta = 0xff75d6e8;
    constexpr std::uint32_t spectrumDeltaBright = 0xffcdeff5;
    constexpr std::uint32_t spectrumPre = 0xff74808f;
    constexpr std::uint32_t spectrumPost = 0xffa695d6;
    constexpr std::uint32_t ledBlue    = 0xff4488cc;
    constexpr std::uint32_t ledGreen   = 0xff4cc07a;
    constexpr std::uint32_t ledYellow  = 0xffccaa44;
    constexpr std::uint32_t ledGrey    = 0xff555558;

    enum class PreDisplayTone
    {
        context,
        emphasis,
    };

    constexpr std::uint32_t preDisplayPrimaryColour (PreDisplayTone tone) noexcept
    {
        return tone == PreDisplayTone::emphasis ? flora : normal;
    }

    constexpr std::uint32_t preDisplayDetailColour (PreDisplayTone tone) noexcept
    {
        return tone == PreDisplayTone::emphasis ? flora : preDisplayContextDetail;
    }

    struct Rect
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    constexpr int right (Rect r) noexcept  { return r.x + r.width; }
    constexpr int bottom (Rect r) noexcept { return r.y + r.height; }

    struct LoudnessSelectorLayout
    {
        int deltaPrefixWidth = 0;
        Rect momentary;
        Rect shortTerm;
    };

    constexpr int loudnessDeltaMaximumPrefixWidth (
        int width = loudnessSelectorWidth) noexcept
    {
        const int available = width - 2 * loudnessSelectorHorizontalInset
                            - 2 * loudnessSegmentMinimumWidth;
        return available > 0 ? available : 0;
    }

    // `measuredGlyphWidth` is ceil(Font::getStringWidthFloat (U+0394)) from the same font used
    // for paint. The old hard-coded 8 px happened to fit SF NS but dropped the entire one-glyph
    // string when Windows selected a wider face. Bound the measured prefix while preserving at
    // least 12 logical pixels for each interactive M/S segment.
    constexpr int loudnessDeltaPrefixWidth (
        bool deltaMode, int measuredGlyphWidth,
        int width = loudnessSelectorWidth) noexcept
    {
        if (! deltaMode)
            return 0;
        const int maximum = loudnessDeltaMaximumPrefixWidth (width);
        const int requested = measuredGlyphWidth > loudnessDeltaMinimumPrefixWidth
            ? measuredGlyphWidth : loudnessDeltaMinimumPrefixWidth;
        return requested < maximum ? requested : maximum;
    }

    constexpr LoudnessSelectorLayout loudnessSelectorLayout (
        bool deltaMode, int measuredGlyphWidth,
        int width = loudnessSelectorWidth,
        int height = metricRowHeight) noexcept
    {
        const int prefix = loudnessDeltaPrefixWidth (deltaMode, measuredGlyphWidth, width);
        const int innerX = prefix + loudnessSelectorHorizontalInset;
        const int innerY = loudnessSelectorVerticalInset;
        const int innerWidth = width - prefix - 2 * loudnessSelectorHorizontalInset;
        const int innerHeight = height - 2 * loudnessSelectorVerticalInset;
        const int firstWidth = innerWidth / 2;
        return {
            prefix,
            { innerX, innerY, firstWidth, innerHeight },
            { innerX + firstWidth, innerY, innerWidth - firstWidth, innerHeight },
        };
    }

    struct PreDisplayDetailLayout
    {
        Rect detail;
        Rect state;
    };

    constexpr PreDisplayDetailLayout preDisplayDetailLayout (
        Rect fullLine, int requestedStateWidth) noexcept
    {
        if (requestedStateWidth <= 0)
            return { fullLine, {} };
        const int maximumStateWidth = fullLine.width - preDisplayStateGap
                                    - preDisplayDetailMinimumWidth;
        if (maximumStateWidth <= 0)
            return { fullLine, {} };
        const int stateWidth = requestedStateWidth > maximumStateWidth
            ? maximumStateWidth : requestedStateWidth;
        return {
            { fullLine.x, fullLine.y,
              fullLine.width - preDisplayStateGap - stateWidth, fullLine.height },
            { fullLine.x + fullLine.width - stateWidth, fullLine.y,
              stateWidth, fullLine.height },
        };
    }

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

    constexpr EditorLayout editorLayout (bool post, int width = editorWidth,
                                         int height = editorHeight) noexcept
    {
        EditorLayout layout {};
        layout.led = { width - margin - ledSize,
                       topSpace + (titleHeight - ledSize) / 2,
                       ledSize,
                       ledSize };
        layout.pairStatus = { width - margin - ledSize - 6 - pairStatusWidth,
                              topSpace,
                              pairStatusWidth,
                              titleHeight };
        // PRE shares this row with its editable name and keeps the established 42 px title slot.
        // POST's name is on the next row, so its title owns all otherwise-empty space up to PAIR.
        layout.title = { margin,
                         topSpace,
                         post ? layout.pairStatus.x - titlePairGap - margin : preTitleWidth,
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
            const int fieldLeft = margin + preTitleWidth + 4;
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
                            height - feedbackHeight - 2,
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

    constexpr Rect spectrumToggleBounds (int width = editorWidth) noexcept
    {
        const int centredOffset = (width - editorWidth) / 2;
        return { centredOffset + margin + preTitleWidth + spectrumTitleGap,
                 topSpace + (titleHeight - spectrumToggleHeight) / 2,
                 spectrumToggleWidth,
                 spectrumToggleHeight };
    }

    constexpr Rect spectrumSizeToggleBounds (int width = editorWidth) noexcept
    {
        const auto toggle = spectrumToggleBounds (width);
        return { right (toggle) + spectrumSizeToggleGap,
                 toggle.y,
                 spectrumSizeToggleWidth,
                 spectrumToggleHeight };
    }

    constexpr Rect spectrumPostControlsBounds (int width = editorWidth, int height = editorHeight) noexcept
    {
        const auto layout = editorLayout (true, width, height);
        return { margin,
                 layout.feedback.y - postControlHeight - 1,
                 width - 2 * margin,
                 postControlHeight };
    }

    constexpr Rect spectrumPlotBounds (int width = editorWidth, int height = editorHeight) noexcept
    {
        const auto layout = editorLayout (true, width, height);
        const auto controls = spectrumPostControlsBounds (width, height);
        return { margin, layout.metricTop, width - 2 * margin,
                 controls.y - 3 - layout.metricTop };
    }

    constexpr float spectrumVisualScale (int plotWidth) noexcept
    {
        return static_cast<float> (plotWidth + 2 * margin)
             / static_cast<float> (editorWidth);
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
    static_assert (loudnessDeltaMaximumPrefixWidth() >= loudnessDeltaMinimumPrefixWidth
                       && loudnessSelectorLayout (true, 10).momentary.width
                              >= loudnessSegmentMinimumWidth
                       && loudnessSelectorLayout (true, 10).shortTerm.width
                              >= loudnessSegmentMinimumWidth,
                   "Delta and both M/S hit targets must fit the fixed loudness selector");
    static_assert (menuFontHeight >= 16.0f && pairMenuItemHeight >= 28
                       && pairMenuMinimumWidth >= editorWidth && pairMenuMaximumColumns == 1,
                   "The pair menu must remain readable and single-column in every plugin format");
    static_assert (bottom (editorLayout (true).feedback) <= editorHeight,
                   "POST feedback row must fit the 300x200 editor boundary");
    static_assert (bottom (metricCellBounds (5, editorLayout (true).metricTop))
                       < editorLayout (true).postControls.y,
                   "POST metrics and controls must not overlap");
    static_assert (right (spectrumToggleBounds()) < editorLayout (true).pairStatus.x,
                   "POST Spectrum mode control must not overlap pair status");
    static_assert (spectrumSizePresets[0].width == editorWidth
                       && spectrumSizePresets[0].height == editorHeight
                       && spectrumSizePresets[1].width == 375 && spectrumSizePresets[1].height == 250
                       && spectrumSizePresets[2].width == 450 && spectrumSizePresets[2].height == 300,
                   "POST Spectrum must expose only the fixed 100/125/150 percent sizes");
    static_assert (right (spectrumSizeToggleBounds()) < editorLayout (true).pairStatus.x
                       && right (spectrumSizeToggleBounds (375)) < editorLayout (true, 375, 250).pairStatus.x
                       && right (spectrumSizeToggleBounds (450)) < editorLayout (true, 450, 300).pairStatus.x,
                   "POST Spectrum size control must never overlap pair status");
    static_assert (bottom (spectrumPlotBounds()) < spectrumPostControlsBounds().y,
                   "POST Spectrum plot and controls must not overlap");
    static_assert (bottom (spectrumPlotBounds (375, 250)) < spectrumPostControlsBounds (375, 250).y
                       && bottom (spectrumPlotBounds (450, 300)) < spectrumPostControlsBounds (450, 300).y,
                   "Expanded Spectrum plots and controls must not overlap");
    static_assert (spectrumPostControlsBounds().x == editorLayout (true).postControls.x
                       && spectrumPostControlsBounds().y == editorLayout (true).postControls.y
                       && spectrumPostControlsBounds().width == editorLayout (true).postControls.width
                       && spectrumPostControlsBounds().height == editorLayout (true).postControls.height,
                   "Compact Spectrum must retain the established control geometry");
    static_assert (spectrumVisualScale (spectrumPlotBounds().width) == 1.0f
                       && spectrumVisualScale (spectrumPlotBounds (450, 300).width) == 1.5f,
                   "Spectrum visual scale must follow the exact fixed window widths");
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
    static_assert (preDisplayPrimaryColour (PreDisplayTone::context)
                       != preDisplayPrimaryColour (PreDisplayTone::emphasis)
                       && preDisplayDetailColour (PreDisplayTone::context)
                       != preDisplayDetailColour (PreDisplayTone::emphasis),
                   "Only a factual PRE section or bounded positional cue has emphasis tone");
    static_assert (editorLayout (false).name.width >= 160,
                   "Every valid 16-character PRE name must fit at the release font size");
    static_assert (watchMetrics[1].maximum && watchMetrics[3].maximum && watchMetrics[5].maximum,
                   "Watch right column must remain MAX for all three metrics");
    static_assert (! recordMetrics[2].deltaEligible && ! recordMetrics[3].deltaEligible,
                   "Record Max TP and I are absolute session values in PRE and POST");
}
