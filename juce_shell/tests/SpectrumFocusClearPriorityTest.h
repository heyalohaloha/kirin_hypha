#pragma once
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include <cstdlib>
#include <iostream>
namespace hypha::tests
{
inline void verifyFocusClearPriority (SpectrumComponent& component, juce::Rectangle<float> plot,
                                     float scale, juce::Time eventTime)
{
    const auto clear = spectrum_geometry::focusClearBoundsFor (
        spectrum_geometry::readoutBoundsFor (plot, scale, scale > 1.1f, true), scale);
    const auto point = clear.getCentre();
    component.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {},
        0, 0, 0, 0, 0, &component, &component, eventTime, point, eventTime, 0, false });
    if (component.hasFocusLock() || component.isShapeObservationForTest())
    { std::cerr << "Focus clear must win over the overlapping SHAPE selector\n"; std::exit (1); }
}
}
