#pragma once

#include <array>

namespace hypha::ui_contract
{
    constexpr int spectrumPresentationHz = 30;
    constexpr int spectrumCurvePresentationHz = 12;
    constexpr int perceptualCurvePresentationHz = 5;
    constexpr int analysisNumericPresentationHz = 2;
    constexpr int spectrumToggleWidth = 84;
    constexpr int spectrumToggleHeight = 21;
    constexpr int spectrumTitleGap = 8;
    constexpr int spectrumSizeToggleGap = 6;
    constexpr int spectrumSizeToggleWidth = 46;
    constexpr int analysisMetersToggleWidth = 54;
    constexpr int analysisModeToggleWidth = 46;
    constexpr int analysisSizeToggleWidth = 40;
    constexpr int analysisHeaderGap = 4;

    struct SpectrumSizePreset
    {
        int width;
        int height;
        const char* buttonText;
        const char* tooltip;
    };

    constexpr std::array<SpectrumSizePreset, 3> spectrumSizePresets {{
        { 300, 200, "100%", "Analysis size: 100%" },
        { 375, 250, "125%", "Analysis size: 125%" },
        { 450, 300, "150%", "Analysis size: 150%" },
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
    constexpr int spectrumExpandedReadoutWidth = 198;
    constexpr int spectrumHoverReadoutHeight = 15;
    constexpr int spectrumHoverReadoutInset = 2;
    constexpr int spectrumHoverFrequencyX = 6;
    constexpr int spectrumHoverFrequencyWidth = 52;
    constexpr int spectrumHoverDeltaX = 58;
    constexpr int spectrumHoverDeltaWidth = 34;
    constexpr int spectrumExpandedFrequencyX = 6;
    constexpr int spectrumExpandedFrequencyWidth = 46;
    constexpr int spectrumExpandedPreX = 54;
    constexpr int spectrumExpandedPreWidth = 45;
    constexpr int spectrumExpandedPostX = 101;
    constexpr int spectrumExpandedPostWidth = 49;
    constexpr int spectrumExpandedDeltaX = 152;
    constexpr int spectrumExpandedDeltaWidth = 34;
    constexpr float spectrumHoverReadoutRadius = 4.5f;
    constexpr float spectrumHoverLineWidth = 0.75f;
    constexpr int spectrumFocusClearWidth = 12;
    constexpr int spectrumChannelModeTop = 1;
    constexpr int spectrumChannelModeHeight = 13;
    constexpr int spectrumChannelModeGap = 2;
    constexpr std::array<int, 3> spectrumChannelModeWidths { 20, 26, 30 };
    constexpr int spectrumLegendAfterChannelModes = 84;
    constexpr int spectrumMarkWidth = 42;
    constexpr int spectrumMarkClearWidth = 11;
    // MARK is one frozen full-band reference. It must remain visibly distinct from PRE/POST
    // while staying below the live 2.15 px Δ curve, which also owns fill and glow.
    constexpr float spectrumMarkCurveAlpha = 0.88f;
    constexpr float spectrumMarkStrokeWidth = 1.50f;
    constexpr float spectrumMarkButtonInactiveAlpha = 0.78f;
    constexpr float spectrumMarkButtonActiveAlpha = 0.98f;
    constexpr float spectrumMarkButtonInactiveBorderAlpha = 0.34f;
    constexpr float spectrumMarkButtonActiveBorderAlpha = 0.82f;
    constexpr float spectrumMarkButtonActiveFillAlpha = 0.13f;
    constexpr float spectrumFocusTrailCompactHeight = 15.0f;
    constexpr float spectrumFocusTrailMediumHeight = 22.0f;
    constexpr float spectrumFocusTrailLargeHeight = 30.0f;
    constexpr float spectrumFocusTrailAxisGap = 12.0f;
    constexpr float spectrumFocusTrailInset = 3.0f;
    constexpr float spectrumFocusTrailRadius = 4.0f;
    constexpr float spectrumFocusTrailStrokeWidth = 1.35f;
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

    constexpr float spectrumFocusTrailHeight (float visualScale) noexcept
    {
        return visualScale < 1.125f ? spectrumFocusTrailCompactHeight
             : visualScale < 1.375f ? spectrumFocusTrailMediumHeight
                                    : spectrumFocusTrailLargeHeight;
    }

    constexpr const char* spectrumShowTooltip = "Show POST - PRE analysis";
    constexpr const char* spectrumHideTooltip = "Return to meters";
    constexpr int spectrumTooltipMaximumCharacters = 25;

    constexpr const char* spectrumTooltip (bool spectrumMode) noexcept
    {
        return spectrumMode ? spectrumHideTooltip : spectrumShowTooltip;
    }
}
