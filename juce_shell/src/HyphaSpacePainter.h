#pragma once

#include <juce_graphics/juce_graphics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::space_field
{
// Renders only the rolling POST stereo facts carried by KirinMeterSession. The density is a
// shape-normalized MID/SIDE observation; absolute signal magnitude remains owned by LEVEL.
void paint (juce::Graphics&,
            juce::Rectangle<int> area,
            const KirinMeterSession&,
            bool available);
}
