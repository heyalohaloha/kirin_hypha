#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaSpectrumFocusTrail.h"

namespace hypha::spectrum_focus_painter
{
    void paint (juce::Graphics& graphics,
                juce::Rectangle<float> bounds,
                float visualScale,
                const spectrum_focus::FocusTrailHistory& history,
                float normalisedBand,
                bool compact);
}
