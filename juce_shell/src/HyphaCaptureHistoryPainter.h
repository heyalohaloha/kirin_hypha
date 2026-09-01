#pragma once

#include <vector>

#include <juce_graphics/juce_graphics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::capture_history
{
// Removes observations newer than the already displayed parent frame. This lets a user-triggered
// history poll enrich Capture without mixing a later audio callback into the frozen UI fact.
void retainThrough (std::vector<KirinMeterHistoryEntry>&, std::uint64_t observedFrames);

// A deliberately narrow 60-second history for the LEVEL Observation Plate. It preserves the
// measured M/S trajectory and run discontinuities without turning LEVEL into a second TIME UI.
void paint (juce::Graphics&,
            juce::Rectangle<int>,
            const std::vector<KirinMeterHistoryEntry>&,
            bool delta,
            juce::String contextFact = {});
}
