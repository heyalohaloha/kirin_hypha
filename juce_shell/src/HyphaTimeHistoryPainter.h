#pragma once

#include <vector>

#include <juce_graphics/juce_graphics.h>

#include "HyphaMeterContext.h"
#include "HyphaPresentationContext.h"
#include "kirin_hypha_ffi.h"

namespace hypha::time_history
{
struct HistoryAxis;
int auxLabelWidth (presentation::Context, bool plr, bool delta, int availableWidth);
juce::Range<float> dataXRange (juce::Rectangle<int> area, bool compactMeter) noexcept;
float dataXForEntry (juce::Range<float>, const KirinMeterHistoryEntry&, const HistoryAxis&,
                     std::size_t index, std::size_t count) noexcept;
// Draws only retained Meter Session facts. Every curve and aggregate span in one column comes
// from the same KirinMeterHistoryEntry and therefore shares its run and sample endpoint. Metric
// availability remains independent: a missing 400 ms M value must not erase a valid 3 s S value.
void paint (juce::Graphics&,
            juce::Rectangle<int> area,
            const std::vector<KirinMeterHistoryEntry>&,
            const juce::String& rangeLabel,
            bool delta,
            bool compactMeter,
            meter_context::ScaleMode scaleMode,
            presentation::Context);
}
