#pragma once

#include <BinaryData.h>

namespace hypha::tests
{
inline void verifyObservatoryBackdropContract()
{
    observatory_world::State probeState;
    probeState.role = observatory::Role::post;
    probeState.density = observatory::Density::observatory;
    probeState.active = true;
    const auto postActive = observatory_world::backdropOpacity (probeState);
    probeState.active = false;
    const auto postInactive = observatory_world::backdropOpacity (probeState);
    probeState.role = observatory::Role::pre;
    const auto preInactive = observatory_world::backdropOpacity (probeState);
    probeState.density = observatory::Density::compact;
    const auto minimumBackdrop = observatory_world::backdropOpacity (probeState);
    KIRIN_OBSERVATORY_REQUIRE (postActive >= 0.95f && postActive <= 1.0f);
    KIRIN_OBSERVATORY_REQUIRE (postInactive > preInactive);
    KIRIN_OBSERVATORY_REQUIRE (preInactive > minimumBackdrop);
    KIRIN_OBSERVATORY_REQUIRE (minimumBackdrop >= 0.50f);

    probeState.role = observatory::Role::post;
    probeState.active = true;
    const auto activeSpecimen = observatory_world::hyphaSpecimenOpacity (probeState);
    probeState.active = false;
    const auto inactiveSpecimen = observatory_world::hyphaSpecimenOpacity (probeState);
    KIRIN_OBSERVATORY_REQUIRE (activeSpecimen > inactiveSpecimen);
    KIRIN_OBSERVATORY_REQUIRE (activeSpecimen <= 0.32f);
    KIRIN_OBSERVATORY_REQUIRE (inactiveSpecimen <= 0.20f);

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
