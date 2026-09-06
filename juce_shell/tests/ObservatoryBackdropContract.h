#pragma once

#include <BinaryData.h>

namespace hypha::tests
{
inline void verifyObservatoryBackdropContract()
{
    const auto source = juce::ImageFileFormat::loadFrom (
        BinaryData::observatory_understory_png,
        static_cast<size_t> (BinaryData::observatory_understory_pngSize));
    observatory_world::Backdrop retained;
    for (const int width : { 300, 450, 900, 300 })
        for (const float scale : { 1.0f, 1.5f, 2.0f, 1.0f })
            for (const bool active : { true, false })
            {
                const juce::Rectangle<int> area (0, 0, width, width * 2 / 3);
                observatory_world::State state;
                state.active = active;
                state.role = active ? observatory::Role::post : observatory::Role::pre;
                state.density = width < 450 ? observatory::Density::compact
                                           : observatory::Density::observatory;
                const auto render = [&] (bool cached)
                {
                    juce::Image result (juce::Image::ARGB,
                        juce::roundToInt (area.getWidth() * scale),
                        juce::roundToInt (area.getHeight() * scale), true);
                    juce::Graphics graphics (result);
                    graphics.addTransform (juce::AffineTransform::scale (scale));
                    if (cached)
                        retained.draw (graphics, area, state);
                    else
                    {
                        graphics.fillAll (BG);
                        graphics.setOpacity (observatory_world::backdropOpacity (state));
                        observatory_world::drawAspectFill (graphics, source, area);
                    }
                    return result;
                };
                const auto expected = render (false);
                const auto actual = render (true);
                const auto repeated = render (true);
                double error = 0.0;
                for (int y = 0; y < expected.getHeight(); ++y)
                    for (int x = 0; x < expected.getWidth(); ++x)
                    {
                        const auto a = actual.getPixelAt (x, y);
                        const auto b = expected.getPixelAt (x, y);
                        KIRIN_OBSERVATORY_REQUIRE (a == repeated.getPixelAt (x, y));
                        error += std::abs (static_cast<int> (a.getRed()) - b.getRed());
                        error += std::abs (static_cast<int> (a.getGreen()) - b.getGreen());
                        error += std::abs (static_cast<int> (a.getBlue()) - b.getBlue());
                    }
                // One extra alpha compositing step may round a channel by one level.
                KIRIN_OBSERVATORY_REQUIRE (
                    error / (3.0 * expected.getWidth() * expected.getHeight()) < 1.0);
            }
}
}
