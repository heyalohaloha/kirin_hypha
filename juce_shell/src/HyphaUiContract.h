#pragma once

#include <array>
#include <cstdint>

#include "HyphaSpectrumUiContract.h"
// Pure C++ release UI contract. This header deliberately has no JUCE dependency so the exact
// shared typography, palette, menu, and analysis constants can be validated before building either
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
    constexpr int pairDropdownGap   = 4;
    constexpr int compactPairNameMinimumWidth = 54;
    constexpr int preDisplayLineHeight = 18;
    constexpr int preDisplayStateGap = 4;
    constexpr int preDisplayDetailMinimumWidth = 72;
    constexpr int preDisplayPresentationHz = 10;
    constexpr int absoluteTimelineSourceHz = 10;
    // PopupMenu is a separate native window in desktop AU/VST3 hosts. Its geometry therefore
    // cannot inherit the 300x200 editor scale and must be explicit in the shared contract.
    constexpr int pairMenuItemHeight     = 28;
    constexpr int pairMenuMinimumWidth   = editorWidth;
    constexpr int pairMenuMaximumColumns = 1;
    // Waldenburg Book is Hypha's product typeface. The paid file is never committed to this GPL
    // repository: licensed release builds inject it through the CMake font boundary. Native faces
    // remain a deterministic test/development fallback, never an acceptable release substitute.
    constexpr const char* kimeraFontFamily = "KMR Waldenburg Book";
    constexpr const char* fallbackLabelFontFamily = ".SF NS";
    constexpr const char* fallbackMonoFontFamily  = ".SF NS Mono";
    constexpr const char* windowsFallbackLabelFontFamily = "Segoe UI";
    constexpr const char* windowsFallbackMonoFontFamily  = "Consolas";
    constexpr float titleFontHeight       = 20.0f;
    constexpr float pairStatusFontHeight  = 13.0f;
    constexpr float feedbackFontHeight    = 13.0f;
    constexpr float preDisplayPrimaryFontHeight = 12.0f;
    constexpr float preDisplayDetailFontHeight = 11.0f;
    constexpr float nameFontHeight        = 16.0f;
    constexpr float menuFontHeight        = 16.0f;
    constexpr float framedButtonFontHeight   = 15.0f;
    constexpr float framelessButtonFontHeight = 13.0f;

    constexpr const char* preTitle = "PRE";
    constexpr const char* postTitle = "POST";
    constexpr std::uint32_t background = 0xff16110d;
    constexpr std::uint32_t normal     = 0xffe8e2d8;
    // Full LEVEL Observatory numerals use the concept image's warm instrument ivory. Compact
    // meters retain `normal` for maximum small-size contrast.
    constexpr std::uint32_t observatoryValue = 0xfff0e4cc;
    constexpr std::uint32_t muted      = 0xff6b6158;
    constexpr std::uint32_t preDisplayContextDetail = 0xff898989;
    constexpr std::uint32_t flora      = 0xffc9a15a;
    constexpr std::uint32_t floraBright = 0xfff3d7a0;
    // Guide gold is a semantic alias, not a quality scale: it identifies received Kirin OS facts.
    constexpr std::uint32_t guideGold = flora;
    constexpr std::uint32_t guideGoldBright = floraBright;
    constexpr std::uint32_t spectrumDelta = 0xff7fcfd8;
    constexpr std::uint32_t spectrumDeltaBright = 0xffd3eff3;
    constexpr std::uint32_t spectrumPre = 0xff968c80;
    constexpr std::uint32_t spectrumPost = 0xffe0bd7e;
    constexpr std::uint32_t spectrumMid = 0xff7fcfd8;
    constexpr std::uint32_t spectrumSide = 0xffad9fdc;
    // Sharpness keeps one identity colour on every page (LIVE, DRUM). Lilac is a small-mark and
    // thin-line colour only; it never fills an area.
    constexpr std::uint32_t sharpness = 0xffb3a2e6;
    constexpr std::uint32_t ledBlue    = 0xff7fcfd8;
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

    constexpr Rect analysisMetersToggleBounds (int width = editorWidth) noexcept
    {
        const int centredOffset = (width - editorWidth) / 2;
        return { centredOffset + margin + preTitleWidth + spectrumTitleGap,
                 topSpace + (titleHeight - spectrumToggleHeight) / 2,
                 analysisMetersToggleWidth,
                 spectrumToggleHeight };
    }

    constexpr Rect analysisModeToggleBounds (int width = editorWidth) noexcept
    {
        const auto meters = analysisMetersToggleBounds (width);
        return { right (meters) + analysisHeaderGap, meters.y,
                 analysisModeToggleWidth, spectrumToggleHeight };
    }

    constexpr Rect analysisSizeToggleBounds (int width = editorWidth) noexcept
    {
        const auto mode = analysisModeToggleBounds (width);
        return { right (mode) + analysisHeaderGap, mode.y,
                 analysisSizeToggleWidth, spectrumToggleHeight };
    }

    // Analysis component tests use the same content aperture the product Observatory exposes.
    // It is independent of the retired six-cell meter layout.
    constexpr Rect spectrumPlotBounds (int width = editorWidth,
                                       int height = editorHeight) noexcept
    {
        return { margin, 67, width - 2 * margin, height - 121 };
    }

    constexpr float spectrumVisualScale (int plotWidth) noexcept
    {
        return static_cast<float> (plotWidth + 2 * margin)
             / static_cast<float> (editorWidth);
    }
    static_assert (nameFontHeight >= 16.0f
                       && pairStatusFontHeight >= 13.0f,
                   "The 300x200 editor must retain the legibility floor agreed for release");
    static_assert (pairDropdownWidth >= 28 && pairDropdownGap >= 4
                       && compactPairNameMinimumWidth >= pairStatusWidth,
                   "PAIR text and its independent menu target must never share a rectangle");
    static_assert (menuFontHeight >= 16.0f && pairMenuItemHeight >= 28
                       && pairMenuMinimumWidth >= editorWidth && pairMenuMaximumColumns == 1,
                   "The pair menu must remain readable and single-column in every plugin format");
    static_assert (spectrumSizePresets[0].width == editorWidth
                       && spectrumSizePresets[0].height == editorHeight
                       && spectrumSizePresets[1].width == 375 && spectrumSizePresets[1].height == 250
                       && spectrumSizePresets[2].width == 450 && spectrumSizePresets[2].height == 300
                       && spectrumSizePresets[3].width == 600 && spectrumSizePresets[3].height == 400
                       && spectrumSizePresets[4].width == 900 && spectrumSizePresets[4].height == 600,
                   "POST Analysis must expose only the fixed 100/125/150/200/300 percent sizes");
    static_assert (spectrumVisualScale (280) == 1.0f
                       && spectrumVisualScale (430) == 1.5f
                       && spectrumVisualScale (580) == 2.0f
                       && spectrumVisualScale (880) == 3.0f,
                   "Spectrum visual scale must follow the exact fixed window widths");
    static_assert (preDisplayPrimaryColour (PreDisplayTone::context)
                       != preDisplayPrimaryColour (PreDisplayTone::emphasis)
                       && preDisplayDetailColour (PreDisplayTone::context)
                       != preDisplayDetailColour (PreDisplayTone::emphasis),
                   "Only a factual PRE section or bounded positional cue has emphasis tone");
}
