#pragma once

#include "AttackUiChromeContract.h"
#include "../src/HyphaMainFrame.h"

#include <array>
#include <cmath>
#include <iostream>

namespace hypha::attack_ui_test
{
inline bool verifyFrameGeometry()
{
    // Rendered active/dormant headers must have identical chips: observation chrome may not
    // paint over a control. A dormant body supplies the same header without a main frame.
    for (const auto& preset : observatory::sizePresets)
    {
        const auto shell = observatory::shellLayout (observatory::Role::post, preset,
                                                     observatory::GuidePresence::absent);
        ChromeState state;
        state.width = shell.body.width;
        state.height = shell.body.height - observatory::timeNavigationHeight (preset.density);
        state.context = presentation::forEditor (preset.width, preset.height);
        const auto shape = attack_ui::layoutFor (state.width, state.height, state.context);
        if (attack_band::panesShown (shape) != (preset.width >= 600))
        {
            std::cerr << "DRUM frame changes the admitted HEAD/TAIL pane sizes at " << preset.width << '\n';
            return false;
        }
        const auto window = rectangle (attack_ui::historyWindow (shape));
        const auto row = rectangle (shape.history);
        const auto measures = main_frame::measuresFor (key_light::inEditor (state.context));
        const auto extent = window.expanded (static_cast<int> (std::ceil (measures.ring)));
        if (window.isEmpty() || ! row.contains (extent)
            || extent.getY() < shape.header.bottom()
            || (shape.arrangement == attack_ui::Arrangement::lanes
                && ! attack_ui::sharesOnePlotColumn (shape)))
        {
            std::cerr << "DRUM bevel leaves its row or breaks the shared time column at " << preset.width << '\n';
            return false;
        }
        for (const std::uint8_t band : { std::uint8_t { 0 }, std::uint8_t { 4 } })
            for (const float dpi : { 1.0f, 2.0f })
            {
                state.band = band;
                state.running = true;
                state.dpi = dpi;
                auto component = std::make_unique<AttackComponent>();
                applyChromeState (*component, state);
                const key_light::CoordinateScope coordinates (*component, state.context, {});
                const auto active = renderAttack (*component, dpi);
                state.running = false;
                applyChromeState (*component, state);
                const auto dormant = renderAttack (*component, dpi);
                auto chips = rectangle (attack_band::chipRow (shape, state.context));
                chips = { juce::roundToInt (chips.getX() * dpi), juce::roundToInt (chips.getY() * dpi),
                          juce::roundToInt (chips.getWidth() * dpi), juce::roundToInt (chips.getHeight() * dpi) };
                if (! chips.isEmpty() && differences (active, dormant, chips) != 0)
                {
                    std::cerr << "DRUM observation frame changes header chips at " << preset.width << '\n';
                    return false;
                }
                if (shape.arrangement != attack_ui::Arrangement::glance) continue;
                // The compact frame must have all four sides, including the top and side rims
                // that used to fall outside the component. Inspect only frame pixels.
                const auto rim = juce::jmax (1, static_cast<int> (std::floor (measures.ring)) - 1);
                const std::array strips {
                    juce::Rectangle<int> (window.getX(), window.getY() - rim, window.getWidth(), rim),
                    juce::Rectangle<int> (window.getX() - rim, window.getY(), rim, window.getHeight()),
                    juce::Rectangle<int> (window.getRight(), window.getY(), rim, window.getHeight()),
                    juce::Rectangle<int> (window.getX(), window.getBottom(), window.getWidth(), rim),
                };
                for (auto strip : strips)
                {
                    strip = { juce::roundToInt (strip.getX() * dpi), juce::roundToInt (strip.getY() * dpi),
                              juce::roundToInt (strip.getWidth() * dpi), juce::roundToInt (strip.getHeight() * dpi) };
                    if (! active.getBounds().contains (strip) || differences (active, dormant, strip) < 10)
                    {
                        std::cerr << "DRUM compact frame is missing a complete rim\n";
                        return false;
                    }
                }
            }
    }
    return true;
}
}
