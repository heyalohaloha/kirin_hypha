#pragma once

#include <array>
#include <cmath>
#include <cstdlib>

namespace hypha::attack_ui_test
{
inline bool nearColour (juce::Colour pixel, juce::Colour target)
{
    constexpr int tolerance = 12;
    return pixel.getAlpha() > 16
        && std::abs ((int) pixel.getRed() - (int) target.getRed()) <= tolerance
        && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) <= tolerance
        && std::abs ((int) pixel.getBlue() - (int) target.getBlue()) <= tolerance;
}

inline bool verifyNoSelectionBar (const juce::Image& image)
{
    const auto target = juce::Colour (attack_ui::selectionColour);
    const auto top = attack_ui::headerHeight;
    const auto height = attack_ui::timelineHeight (image.getHeight());
    for (int x = 0; x < image.getWidth(); ++x)
    {
        int run = 0;
        for (int y = top; y < top + height; ++y)
        {
            run = nearColour (image.getPixelAt (x, y), target) ? run + 1 : 0;
            if (run > juce::jmax (10, height / 3))
                return false;
        }
    }
    return true;
}

inline bool verifyContinuousScrubRail (const juce::Image& image)
{
    const auto scrubTop = attack_ui::headerHeight
                        + attack_ui::timelineHeight (image.getHeight());
    const auto railY = scrubTop + attack_ui::axisLabelHeight / 2 - 2;
    for (int x = 35; x < image.getWidth() - 35; ++x)
        if (image.getPixelAt (x, railY).getAlpha() == 0)
            return false;
    return true;
}

inline bool verifyDormantSpecimenBlack (const juce::Image& image)
{
    const auto height = attack_ui::metricsHeight (image.getHeight());
    if (height == 0)
        return true;
    const auto area = juce::Rectangle<int> (0, image.getHeight() - height,
                                            image.getWidth(), height).reduced (5);
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (image.getPixelAt (x, y) != juce::Colours::black)
                return false;
    return true;
}

inline bool verifyNoMetricLeaderCorridors (const juce::Image& image)
{
    const auto height = attack_ui::metricsHeight (image.getHeight());
    if (height == 0 || image.getWidth() < 390)
        return true;
    auto metrics = juce::Rectangle<int> (
        0, image.getHeight() - height, image.getWidth(), height).reduced (1);
    auto content = metrics.reduced (7, 3);
    content.removeFromTop (12);
    if (content.getHeight() < 65)
        return true;
    const auto scale = attack_ui::textScale (image.getWidth(), image.getHeight());
    const auto sideWidth = juce::jmin (scale > 1.4f ? 178 : 112, content.getWidth() / 4);
    const auto left = juce::Rectangle<int> (
        content.getX() + sideWidth, content.getY(), 4, content.getHeight());
    const auto right = juce::Rectangle<int> (
        content.getRight() - sideWidth - 4, content.getY(), 4, content.getHeight());
    for (const auto corridor : { left, right })
        for (int y = corridor.getY(); y < corridor.getBottom(); ++y)
            for (int x = corridor.getX(); x < corridor.getRight(); ++x)
                if (image.getPixelAt (x, y) != juce::Colours::black)
                    return false;
    return true;
}

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

inline bool verifySupportedSizes (AttackComponent& component)
{
    constexpr std::array<const char*, 5> splitPreviewVariables {{
        "KIRIN_ATTACK_UI_100_SPLIT_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_125_SPLIT_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_150_SPLIT_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_200_SPLIT_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_300_SPLIT_PREVIEW_PATH",
    }};
    constexpr std::array<const char*, 5> overlayPreviewVariables {{
        "KIRIN_ATTACK_UI_100_OVERLAY_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_125_OVERLAY_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_150_OVERLAY_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_200_OVERLAY_PREVIEW_PATH",
        "KIRIN_ATTACK_UI_300_OVERLAY_PREVIEW_PATH",
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
            if (! verifyNoSelectionBar (image) || ! verifyContinuousScrubRail (image)
                || ! verifyNoMetricLeaderCorridors (image))
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
