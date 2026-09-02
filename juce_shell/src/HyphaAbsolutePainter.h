#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::absolute_painter
{
struct PaintState
{
    const KirinAbsoluteBatch& batch;
    const KirinAbsoluteView& numericSnapshot;
    const juce::String& analysisOwnerNames;
    bool haveBatch;
    bool haveNumericSnapshot;
};

// LIVE keeps the measurement fact unavailable (NaN) while presenting it at the fixed scale's
// lower boundary. This prevents a factual below-floor/silent aperture from looking like a broken
// Windows paint path; no replacement value is stored or exposed in the numeric readout.
double displayValueOrFloor (double measuredValue, double displayMinimum) noexcept;
juce::String factValueText (double measuredValue, int decimals);

void paint (juce::Graphics&, juce::Rectangle<float>, const PaintState&);
}
