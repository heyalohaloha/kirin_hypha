#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::absolute_painter
{
struct PaintState
{
    const KirinAbsoluteBatch& batch;
    const KirinAbsoluteView& numericSnapshot;
    bool haveBatch;
    bool haveNumericSnapshot;
};

void paint (juce::Graphics&, juce::Rectangle<float>, const PaintState&);
}
