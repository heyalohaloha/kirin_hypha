#pragma once

#include "../src/HyphaPsbPainter.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSurfaceMaterial.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace psb_light_contract
{
inline void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "PSB light contract: " << message << '\n';
        std::exit (EXIT_FAILURE);
    }
}

inline void selectPsb (SpectrumComponent& component)
{
    const auto bounds = component.getLocalBounds().toFloat();
    const auto toggle = spectrum_geometry::subviewBoundsFor (
        spectrum_geometry::plotBoundsFor (bounds), spectrum_geometry::visualScaleFor (bounds));
    require (! toggle.isEmpty(), "PSB is selected through the existing visible control");
    const auto time = juce::Time::getCurrentTime();
    component.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(),
        toggle.getCentre(), {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &component, &component, time, toggle.getCentre(), time, 1, false });
    require (component.isPsbObservationForTest(), "Spectrum routes the explicit PSB selection");
}

inline KirinPsbView observation()
{
    KirinPsbView frame {};
    frame.status = KIRIN_SPECTRUM_ACTIVE;
    frame.has_data = 1u;
    frame.channels = 2u;
    frame.sample_rate = 48'000u;
    frame.aperture_samples = 4'800u;
    frame.presentation_end_samples = 4'800;
    for (size_t band = 0; band < psb_painter::bandCount; ++band)
        frame.shares[band] = (double) (band + 1u) / 210.0;
    return frame;
}

inline int bronzePixels (const juce::Image& image, juce::Rectangle<float> window,
                         float ring, float dpi)
{
    int count = 0;
    const auto strip = juce::Rectangle<float> (window.getX() + 2.0f,
        window.getY() - ring + 1.0f, window.getWidth() - 4.0f, ring - 1.0f)
        .transformedBy (juce::AffineTransform::scale (dpi)).toNearestInt()
        .getIntersection (image.getBounds());
    for (int y = strip.getY(); y < strip.getBottom(); ++y)
        for (int x = strip.getX(); x < strip.getRight(); ++x)
        {
            const auto pixel = image.getPixelAt (x, y);
            if ((int) pixel.getRed() > (int) pixel.getBlue() + 8
                && (int) pixel.getRed() > (int) pixel.getGreen() + 5) ++count;
        }
    return count;
}

inline void verifyAllSizes()
{
    for (const auto preset : ui_contract::spectrumSizePresets)
    {
        const auto context = presentation::forEditor (preset.width, preset.height);
        const auto body = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) body.width, (float) body.height);
        const auto outer = spectrum_geometry::plotBoundsFor (bounds);
        const auto window = psb_painter::windowBounds (bounds, context);
        const auto plot = psb_painter::dataBounds (bounds, context);
        const auto light = key_light::inEditor (context);
        const auto ring = main_frame::measuresFor (light).ring;
        const auto scale = spectrum_geometry::visualScaleFor (bounds);
        const auto inset = (float) main_frame::insetFor (context);
        require (! plot.isEmpty() && window.contains (plot), "every size keeps a real bar aperture");
        require (window.getY() - ring >= outer.getY() + 30.0f * scale,
                 "the bronze frame never covers the PSB header or axis legend");
        require (window.getBottom() + inset + 14.0f * scale <= bounds.getBottom(),
                 "Bark labels fit below the frame inside the component");
        for (int band = 0; band < (int) psb_painter::bandCount; ++band)
            require (psb_painter::bandAt (bounds,
                { plot.getX() + plot.getWidth() * ((float) band + 0.5f) / (float) psb_painter::bandCount,
                  plot.getCentreY() }, context) == band, "bar centres hit their exact Bark band");
        require (psb_painter::bandAt (bounds, { window.getX() - 1.0f, plot.getCentreY() }, context) == -1
            && psb_painter::bandAt (bounds, { plot.getCentreX(), window.getBottom() + inset }, context) == -1,
                 "the frame and axis label row never select a band");
        for (const float dpi : { 1.0f, 2.0f })
        {
            const auto render = [&] (bool available, bool delta) {
                juce::Component root;
                root.setSize (preset.width, preset.height);
                const key_light::Scope lightScope (root);
                juce::Image image (juce::Image::ARGB, juce::roundToInt (bounds.getWidth() * dpi),
                                   juce::roundToInt (bounds.getHeight() * dpi), true);
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (dpi));
                g.fillAll (BG);
                auto frame = observation();
                std::array<double, psb_painter::bandCount> values;
                for (size_t band = 0; band < values.size(); ++band)
                    values[band] = delta ? (band % 2u == 0u ? 0.04 : -0.04) : frame.shares[band];
                psb_painter::paint (g, bounds, { values, available, delta, -1, "PSB WARMING", context });
                return image;
            };
            const auto waiting = render (false, false);
            require (bronzePixels (waiting, window, ring, dpi) > (int) (window.getWidth() * dpi * 0.5f),
                     "warming PSB retains its main bronze observation frame");
            for (const bool delta : { false, true })
            {
                const auto active = render (true, delta);
                require (bronzePixels (active, window, ring, dpi) > (int) (window.getWidth() * dpi * 0.5f),
                         "absolute and delta PSB share the same main frame");
                const auto lastX = plot.getX() + plot.getWidth() * 19.5f / 20.0f;
                const auto y = delta ? plot.getCentreY() + plot.getHeight() * 0.25f
                                     : plot.getBottom() - plot.getHeight() * 0.45f;
                require (active.getPixelAt (juce::roundToInt (lastX * dpi), juce::roundToInt (y * dpi))
                         != waiting.getPixelAt (juce::roundToInt (lastX * dpi), juce::roundToInt (y * dpi)),
                         "measured bars remain visible inside the framed aperture");
            }
        }
    }
}
}

inline void verifyPsbLightContract()
{
    psb_light_contract::verifyAllSizes();
    SpectrumComponent component;
    component.setSize (880, 479);
    component.setPresentationContext (presentation::forEditor (900, 600));
    component.setAbsoluteObservation (true);
    component.setSignalActive (true);
    psb_light_contract::selectPsb (component);
    component.setPsbSnapshot (psb_light_contract::observation());
    const key_light::CoordinateScope light (component, presentation::forEditor (900, 600), {});
    const auto image = component.createComponentSnapshot (component.getLocalBounds());
    psb_light_contract::require (psb_light_contract::bronzePixels (image,
        psb_painter::windowBounds (component.getLocalBounds().toFloat(), presentation::forEditor (900, 600)),
        7.0f, 1.0f) > 300, "the real Spectrum PSB route paints its frame");
}
}
