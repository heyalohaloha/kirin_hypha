#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaObservatoryContract.h"

namespace hypha::jungle_material
{
void paintInstrumentAcceleration (juce::Graphics&, juce::Rectangle<float>,
                                  observatory::Role, observatory::Density, bool capture);
void paintApertureAcceleration (juce::Graphics&, juce::Rectangle<float>,
                                observatory::Role);
void paintVuAcceleration (juce::Graphics&, juce::Rectangle<float>, observatory::Role);
}
