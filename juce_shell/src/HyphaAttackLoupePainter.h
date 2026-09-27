#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaPresentationContext.h"

namespace hypha::attack_loupe
{
// Inspection-only magnifier of the selected hit over its fixed 150 ms window (20 ms before the
// head to the end of a full body): the 96-point peak shapes over the part that was measured (never
// before the run or past the audio), with the RMS levels that define STRENGTH (the 30 ms head),
// TRANSIENT (the step from the head down to the body, once the hit is complete) and CREST (head
// peak above head RMS). A matched POST is measured at the PRE onset, so both sides share every
// window.
// The panel is cached chrome; `paint` draws only the selected hit.
void paintPanel (juce::Graphics&, juce::Rectangle<int>);
void paint (juce::Graphics&, juce::Rectangle<int>, const KirinAttackDetail* pre,
            const KirinAttackDetail* post, presentation::Context);
}
