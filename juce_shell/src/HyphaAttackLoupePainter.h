#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaPresentationContext.h"

namespace hypha::attack_loupe
{
// Inspection-only magnifier of the selected hit: the measured 96-point peak shapes from 20 ms
// before the window to the end of the full body, with the RMS levels that define STRENGTH (the
// 30 ms head), TRANSIENT (the step from the head down to the body) and CREST (head peak above head
// RMS). A matched POST is measured at the PRE onset, so both sides share every window.
// The panel is cached chrome; `paint` draws only the selected hit.
void paintPanel (juce::Graphics&, juce::Rectangle<int>);
void paint (juce::Graphics&, juce::Rectangle<int>, const KirinAttackDetail* pre,
            const KirinAttackDetail* post, presentation::Context);
}
