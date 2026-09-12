#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
namespace hypha::attack_specimen
{
    struct FeatureAmounts
    {
        float strength = 0.0f;
        float texture = 0.0f;
        float sharpness = 0.0f;
    };

    void drawSpecimen (juce::Graphics&, juce::Rectangle<int>, FeatureAmounts);

}
