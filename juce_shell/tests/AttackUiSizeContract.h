#pragma once

#include <array>
#include <cstdlib>

namespace hypha::attack_ui_test
{
inline bool verifyContinuousTrace (const KirinAttackWaveformBatch& waveform,
                                   const KirinAttackDetailBatch& details)
{
    juce::Image image (juce::Image::ARGB, 360, 80, true);
    juce::Graphics graphics (image);
    attack_painter::drawWaveform (
        graphics, waveform, details, image.getBounds(), 0, 288'000, 48'000,
        attack_painter::WaveformStyle::trace, false, 1.0f);
    bool started = false;
    bool ended = false;
    for (int x = 0; x < image.getWidth(); ++x)
    {
        bool visible = false;
        for (int y = 0; y < image.getHeight(); ++y)
            visible = visible || image.getPixelAt (x, y).getAlpha() > 0;
        if (visible && ended)
            return false;
        if (visible)
            started = true;
        else if (started)
            ended = true;
    }
    return started;
}

inline bool verifySupportedSizes (AttackInternalComponent& component)
{
    constexpr std::array<const char*, 4> splitPreviewVariables {{
        "KIRIN_ATTACK_UI_100_SPLIT_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_125_SPLIT_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_150_SPLIT_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_200_SPLIT_PREVIEW_PATH",
    }};
    constexpr std::array<const char*, 4> overlayPreviewVariables {{
        "KIRIN_ATTACK_UI_100_OVERLAY_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_125_OVERLAY_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_150_OVERLAY_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_200_OVERLAY_PREVIEW_PATH",
    }};
    const auto originalWidth = component.getWidth();
    const auto originalHeight = component.getHeight();
    for (int mode = 0; mode < 2; ++mode)
    {
        component.setOverlayMode (mode == 1);
        const auto& variables = mode == 0 ? splitPreviewVariables : overlayPreviewVariables;
        for (std::size_t index = 0; index < ui_contract::spectrumSizePresets.size(); ++index)
        {
            const auto& preset = ui_contract::spectrumSizePresets[index];
            const auto bounds = ui_contract::spectrumPlotBounds (preset.width, preset.height);
            component.setSize (bounds.width, bounds.height);
            juce::Image image (juce::Image::ARGB, bounds.width, bounds.height, true);
            juce::Graphics graphics (image);
            component.paintEntireComponent (graphics, true);
            if (image.getWidth() != bounds.width || image.getHeight() != bounds.height)
                return false;
            if (const auto* path = std::getenv (variables[index]))
            {
                juce::FileOutputStream output { juce::File { path } };
                juce::PNGImageFormat png;
                if (! output.openedOk() || ! png.writeImageToStream (image, output))
                    return false;
            }
        }
    }
    component.setSize (originalWidth, originalHeight);
    component.setOverlayMode (true);
    return true;
}
}
