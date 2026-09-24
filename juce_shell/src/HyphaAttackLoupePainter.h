#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaPresentationContext.h"

namespace hypha::attack_loupe
{
// Inspection-only magnifier of the selected hit. It draws the measured 96-point peak shapes over
// the exact 100 ms context and 30 ms attack windows, with the RMS levels that define TRANSIENT
// (the step), STRENGTH (the attack level) and CREST (peak above attack RMS). Each side stays at its
// own onset sample, so a PRE/POST onset difference remains visible instead of being aligned away.
// The panel is cached chrome; `paint` draws only the selected hit.
void paintPanel (juce::Graphics&, juce::Rectangle<int>);
void paint (juce::Graphics&, juce::Rectangle<int>, const KirinAttackDetail* pre,
            const KirinAttackDetail* post, presentation::Context);
}
