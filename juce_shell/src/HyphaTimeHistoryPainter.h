#pragma once

#include <vector>

#include <juce_graphics/juce_graphics.h>

#include "HyphaMeterContext.h"
#include "HyphaPresentationContext.h"
#include "kirin_hypha_ffi.h"

namespace hypha::time_history
{
int auxLabelWidth (presentation::Context, bool plr, bool delta, int availableWidth);
// Draws only retained Meter Session facts. Every curve and aggregate span in one column comes
// from the same KirinMeterHistoryEntry and therefore shares its run and sample endpoint.
void paint (juce::Graphics&,
            juce::Rectangle<int> area,
            const std::vector<KirinMeterHistoryEntry>&,
            const juce::String& rangeLabel,
            bool delta,
            bool compactMeter,
            meter_context::ScaleMode scaleMode,
            presentation::Context);
}
