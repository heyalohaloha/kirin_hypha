#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

#include <array>

namespace hypha::reference_ui
{
struct State;

bool configuredReferenceViewsFit (juce::Rectangle<float>, const State&) noexcept;

bool paintConfiguredReferenceViews (juce::Graphics&, juce::Rectangle<float>,
                                    const State&, presentation::Context);

// C の図のうち A の窓の線を描く図の、凡例の A の場所（図の順。描かない図は空）。
std::array<juce::Rectangle<int>, 3> referenceAWindowAreas (juce::Rectangle<float>, const State&, presentation::Context);
}
