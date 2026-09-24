#pragma once
#include "AttackUiImageHelpers.h"
#include "AttackUiPerformanceProbe.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace hypha::attack_ui_test
{
// The selected hit is a thin hypha. No row may contain a selection-coloured highlight bar.
inline bool verifyThinSelection (const juce::Image& image, const attack_ui::Layout& layout)
{
    if (layout.history.empty())
        return true;
    const auto target = juce::Colour (attack_ui::selectionColour);
    const auto history = historyRect (layout);
    const auto bottom = layout.arrangement == attack_ui::Arrangement::lanes
        ? layout.lanes.back().bottom() : layout.axis.bottom();
    for (int y = history.getY(); y < bottom; ++y)
    {
        juce::Rectangle<int> row { history.getX(), y, history.getWidth(), 1 };
        // NOW is text in the selection colour while following LIVE; only its label is exempt.
        if (y >= layout.axis.y && y < layout.axis.bottom())
            row.removeFromRight (attack_ui::axisLabelWidth (layout));
        if (countColour (image, row, target, 40) > 6)
        {
            std::cerr << "selection wider than a hypha at y=" << y << '\n';
            return false;
        }
    }
    return true;
}

inline bool verifyDormantQuiet (const juce::Image& image, const attack_ui::Layout& layout)
{
    const auto area = (layout.arrangement == attack_ui::Arrangement::lanes
        ? lanesArea (layout) : rectangle (layout.line)).reduced (5);
    const std::array semanticColours {
        juce::Colour (attack_ui::selectionColour),
        juce::Colour (attack_ui::strengthColour),
        juce::Colour (attack_ui::crestColour),
        juce::Colour (attack_ui::sharpnessColour),
        juce::Colour (attack_ui::transientColour),
        juce::Colour (attack_ui::waveformColour),
    };
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto pixel = image.getPixelAt (x, y);
            // Dormant lanes may retain the low-contrast CE 2226 material, but never a value,
            // label, selection, or metric colour before data is valid.
            if (juce::jmax (pixel.getRed(), pixel.getGreen(), pixel.getBlue()) > 48)
                return false;
            for (const auto colour : semanticColours)
                if (nearColour (pixel, colour))
                    return false;
        }
    return true;
}

inline bool verifySectionLayout (const attack_ui::Layout& layout, int height)
{
    auto total = layout.header.height + layout.history.height + layout.axis.height;
    if (layout.arrangement != attack_ui::Arrangement::lanes)
        return total + layout.line.height == height;
    for (const auto& lane : layout.lanes)
    {
        total += lane.height;
        if (lane.height != layout.lanes.front().height
            || lane.height < attack_ui::laneMinimumHeight)
            return false;
    }
    return total == height && layout.history.height >= attack_ui::historyMinimumHeight;
}

inline bool verifyContinuousTrace (const KirinAttackWaveformBatch& waveform)
{
    juce::Image image (juce::Image::ARGB, 360, 80, true);
    juce::Graphics graphics (image);
    attack_painter::drawEnvelope (
        graphics, waveform, image.getBounds(), 0, 288'000, 48'000,
        attack_painter::WaveformStyle::trace, 1.0f);
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

// All five editor bodies: one row at 100%/125%, lanes from 150%, the loupe only at 300%.
inline bool verifySupportedSizes (AttackComponent& component)
{
    profileDenseAttackIfRequested();
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
        for (std::size_t index = 0; index < observatory::sizePresets.size(); ++index)
        {
            const auto& preset = observatory::sizePresets[index];
            const auto shell = observatory::shellLayout (observatory::Role::post, preset,
                                                         observatory::GuidePresence::absent);
            const auto width = shell.body.width;
            const auto height = shell.body.height - observatory::timeNavigationHeight (preset.density);
            const auto context = presentation::forEditor (preset.width, preset.height);
            component.setPresentationContext (context);
            component.setSize (width, height);
            const auto image = renderAttack (component);
            const auto layout = attack_ui::layoutFor (width, height, context);
            const auto expected = index < 2 ? attack_ui::Arrangement::line
                                            : attack_ui::Arrangement::lanes;
            if (image.getWidth() != width || image.getHeight() != height
                || layout.arrangement != expected || layout.history.empty()
                || layout.loupe != (index + 1 == observatory::sizePresets.size())
                || ! verifyThinSelection (image, layout)
                || ! verifySectionLayout (layout, height)
                || ! writePreviewTo (variables[index], image))
            {
                std::cerr << "supported size failed: " << width << 'x' << height << '\n';
                return false;
            }
        }
    }
    component.setSize (originalWidth, originalHeight);
    component.setPresentationContext (presentation::defaultContext());
    component.setOverlayMode (true);
    return true;
}
}
