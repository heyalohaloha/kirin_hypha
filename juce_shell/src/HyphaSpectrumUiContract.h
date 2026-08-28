#pragma once

#include <array>

namespace hypha::ui_contract
{
    constexpr int spectrumPresentationHz = 30;
    constexpr int spectrumToggleWidth = 84;
    constexpr int spectrumToggleHeight = 21;
    constexpr int spectrumTitleGap = 8;
    constexpr int spectrumSizeToggleGap = 6;
    constexpr int spectrumSizeToggleWidth = 46;

    struct SpectrumSizePreset
    {
        int width;
        int height;
        const char* buttonText;
        const char* tooltip;
    };

    constexpr std::array<SpectrumSizePreset, 3> spectrumSizePresets {{
        { 300, 200, "100%", "Spectrum size: 100%" },
        { 375, 250, "125%", "Spectrum size: 125%" },
        { 450, 300, "150%", "Spectrum size: 150%" },
    }};

    constexpr int spectrumPlotLeftInset = 24;
    constexpr int spectrumPlotRightInset = 25;
    constexpr int spectrumPlotTopInset = 6;
    constexpr int spectrumPlotBottomInset = 12;
    constexpr float spectrumLegendFontHeight = 8.5f;
    constexpr int spectrumLegendTop = 1;
    constexpr int spectrumLegendHeight = 10;
    constexpr int spectrumDeltaLegendLabelX = 3;
    constexpr int spectrumDeltaLegendLabelWidth = 12;
    constexpr int spectrumPreLegendSampleWidth = 0;
    constexpr int spectrumPreLegendLabelX = 19;
    constexpr int spectrumPreLegendLabelWidth = 20;
    constexpr int spectrumPostLegendSampleWidth = 0;
    constexpr int spectrumPostLegendLabelX = 45;
    constexpr int spectrumPostLegendLabelWidth = 28;
    constexpr float spectrumPreStrokeWidth = 1.10f;
    constexpr float spectrumPreCurveAlpha = 0.92f;
    constexpr float spectrumPostGlowStrokeWidth = 2.60f;
    constexpr float spectrumPostGlowAlpha = 0.05f;
    constexpr float spectrumPostStrokeWidth = 1.10f;
    constexpr float spectrumPostCurveAlpha = 0.70f;
    constexpr float spectrumDeltaLegendAlpha = 0.98f;
    constexpr float spectrumPreLegendAlpha = 0.92f;
    constexpr float spectrumPostLegendAlpha = 0.90f;
    constexpr int spectrumHoverReadoutWidth = 96;
    constexpr int spectrumFocusReadoutWidth = 108;
    constexpr int spectrumExpandedReadoutWidth = 210;
    constexpr int spectrumHoverReadoutHeight = 15;
    constexpr int spectrumHoverReadoutInset = 2;
    constexpr int spectrumHoverFrequencyX = 6;
    constexpr int spectrumHoverFrequencyWidth = 52;
    constexpr int spectrumHoverDeltaX = 58;
    constexpr int spectrumHoverDeltaWidth = 34;
    constexpr int spectrumExpandedFrequencyX = 6;
    constexpr int spectrumExpandedFrequencyWidth = 50;
    constexpr int spectrumExpandedPreX = 58;
    constexpr int spectrumExpandedPreWidth = 49;
    constexpr int spectrumExpandedPostX = 109;
    constexpr int spectrumExpandedPostWidth = 53;
    constexpr int spectrumExpandedDeltaX = 164;
    constexpr int spectrumExpandedDeltaWidth = 34;
    constexpr float spectrumHoverReadoutRadius = 4.5f;
    constexpr float spectrumHoverLineWidth = 0.75f;
    constexpr int spectrumFocusClearWidth = 12;
    constexpr std::array<float, 25> spectrumTipAlpha {
        0.0f, 0.012f, 0.025f, 0.037f, 0.05f, 0.065f, 0.08f,
        0.098f, 0.115f, 0.132f, 0.15f, 0.17f, 0.19f,
        0.212f, 0.235f, 0.258f, 0.28f, 0.302f, 0.325f,
        0.345f, 0.365f, 0.383f, 0.40f, 0.415f, 0.43f };

    constexpr float spectrumStrokeScale (float visualScale) noexcept
    {
        return visualScale < 1.125f ? 1.0f
             : visualScale < 1.375f ? 1.12f
                                    : 1.22f;
    }

    constexpr float spectrumGlowScale (float visualScale) noexcept
    {
        return visualScale < 1.125f ? 1.0f
             : visualScale < 1.375f ? 1.08f
                                    : 1.15f;
    }

    constexpr const char* spectrumShowTooltip = "Show POST - PRE spectrum";
    constexpr const char* spectrumHideTooltip = "Return to meters";
    constexpr int spectrumTooltipMaximumCharacters = 24;

    constexpr const char* spectrumTooltip (bool spectrumMode) noexcept
    {
        return spectrumMode ? spectrumHideTooltip : spectrumShowTooltip;
    }
}
