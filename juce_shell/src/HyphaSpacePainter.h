#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaPresentationContext.h"
#include "kirin_hypha_ffi.h"

namespace hypha::space_field
{
int axisLabelWidth (presentation::Context, bool compact);
int axisLabelHeight (presentation::Context);
// Renders the rolling stereo facts carried by this instance's KirinMeterSession. PRE and POST
// each paint their own, so the two can be compared by switching between the plug-ins. The density
// is a shape-normalized MID/SIDE observation; absolute signal magnitude remains owned by LEVEL.
void paint (juce::Graphics&,
            juce::Rectangle<int> area,
            const KirinMeterSession&,
            bool available,
            bool compactMeter,
            presentation::Context);
}
