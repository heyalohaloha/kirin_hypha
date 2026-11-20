#pragma once

#include "../src/HyphaTruePeakRail.h"
#include "../src/HyphaVuNeedlePainter.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests::true_peak_rail_contract
{
inline void require (bool value, const char* reason)
{
    if (! value) { std::cerr << "True Peak rail: " << reason << '\n'; std::exit (EXIT_FAILURE); }
}

inline void verify()
{
    using namespace hybrid_vu;
    for (const auto dbtp : { -28.0, -24.0, 0.0, 1.5, 3.0, 6.0 })
    {
        const auto expected = std::clamp ((dbtp + 28.0) / 31.0, 0.0, 1.0);
        require (std::abs (truePeakNormalized (dbtp) - expected) < 1.0e-6, "linear -28 to +3 mapping");
    }
    require (truePeakNormalized (std::numeric_limits<double>::quiet_NaN()) == 0.0f, "NaN rests at floor");
    require (peak_rail::colour (0.0) == COL_SPECTRUM_DELTA, "zero retains normal rail colour");
    require (peak_rail::colour (0.001) != peak_rail::colour (0.0), "positive rail changes colour");
    require (peak_rail::colour (0.001, true) != peak_rail::colour (0.0, true), "positive hold changes colour");
    require (peak_rail::colour (1.5) == peak_rail::colour (1.5, true), "positive rail and hold agree");
    // Exercise the actual rail, with clip and hold absent. Even +0.001 changes its live colour.
    for (const auto value : { 0.0, 0.001, 1.5 })
    {
        KirinMeterSession meter {}; KirinWatchDisplay watch {};
        meter.channels = 2; meter.channel_instant_true_peak_dbtp[0] = value;
        State state { observatory::Role::post, meter, watch, true, false };
        // Explicit fill is required: native macOS images may retain alpha even for RGB.
        juce::Image image (juce::Image::RGB, 900, 600, true);
        juce::Graphics graphics (image);
        graphics.fillAll (juce::Colours::black);
        peak_rail::paint (graphics, needle_painter::faceBounds (image.getBounds()), state, 0, false);
        int warm = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto c = image.getPixelAt (x, y);
                const bool isWarm = c.getRed() > c.getBlue() + 25 && c.getRed() > c.getGreen() + 10;
                warm += isWarm;
            }
        const auto correctColour = value > 0.0 ? warm > 0 : warm == 0;
        if (! correctColour)
            std::cerr << "True Peak rail colour: value=" << value << ", warm=" << warm << '\n';
        require (correctColour, "only positive live values use the over-zero colour");
    }
    for (const auto preset : observatory::sizePresets)
    {
        const auto context = presentation::forEditor (preset.width, preset.height);
        const auto face = needle_painter::faceBounds ({ 0, 0, preset.width, preset.height });
        require (peak_rail::labelFontFor (context).getHeight() >= 12.0f, "rail labels stay readable");
        std::vector<peak_rail::Label> all;
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto labels = peak_rail::labels (face, channel, context);
            for (const auto* needed : { "-24", "0", "+3" })
            {
                bool found = false;
                for (const auto& label : labels) found |= label.text == needed;
                require (found, "both scale ends and zero appear at every size");
            }
            for (const auto& label : labels)
            {
                require (face.contains (label.area), "rail label stays inside face");
                for (const auto& previous : all)
                    require (! previous.area.intersects (label.area), "rail labels do not overlap");
                all.push_back (label);
            }
        }
        if (preset.width == 900) require (all.size() == 12, "large face shows all six labels per channel");
    }
}
}
