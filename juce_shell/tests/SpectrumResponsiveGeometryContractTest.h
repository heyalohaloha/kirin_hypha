#pragma once
#include "SpectrumPerformanceFixture.h"
#include "../src/HyphaObservatoryContract.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include <stdexcept>
#include <string>

namespace hypha::tests
{
inline void verifySpectrumResponsiveGeometry()
{
    const auto require = [] (bool value, const char* detail) {
        if (! value) throw std::runtime_error (std::string ("Spectrum geometry: ") + detail);
    };
    for (auto guide : { observatory::GuidePresence::absent, observatory::GuidePresence::present })
    {
        for (const auto& size : observatory::sizePresets)
        {
            const auto layout = observatory::shellLayout (observatory::Role::post, size, guide);
            const juce::Rectangle<float> bounds (0, 0, static_cast<float> (layout.body.width),
                                                       static_cast<float> (layout.body.height));
            const auto absolute = spectrum_geometry::dataPlotBoundsFor (bounds, false);
            const auto delta = spectrum_geometry::dataPlotBoundsFor (bounds, true);
            const auto outer = spectrum_geometry::plotBoundsFor (bounds);
            require (std::abs (absolute.getBottom() - outer.getBottom()) < 0.001f, "absolute bottom");
            require (std::abs (absolute.getY() - delta.getY()) < 0.001f, "shared header");
            require (absolute.getHeight() >= delta.getHeight(), "absolute height");
            if (spectrum_geometry::visualScaleFor (bounds) <= 1.1f) continue;
            require (absolute.getHeight() > delta.getHeight() + 30, "reclaimed focus reservation");

            SpectrumComponent component;
            component.setSize (layout.body.width, layout.body.height);
            component.setAbsoluteObservation (true);
            component.setSignalActive (true);
            component.setSnapshot (spectrumPerformanceFixture());
            // The newly available lower region must work for both hover and click, not just paint.
            const juce::Point<float> point (absolute.getCentreX(), absolute.getBottom() - 4);
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(),
                point, {}, 0, 0, 0, 0, 0, &component, &component, now, point, now, 0, false);
            component.mouseMove (event);
            require (component.getTooltip().isNotEmpty(), "lower-region hover");
            component.mouseDown (event);
            require (component.hasFocusLock(), "lower-region click");
        }
    }
    std::cout << "Spectrum geometry: PASS (actual Observatory body, guide present/absent, POST hit testing)\n";
}
}
