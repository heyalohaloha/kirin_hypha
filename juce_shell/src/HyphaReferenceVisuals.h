#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hypha::reference_ui
{
struct State;

bool paintConfiguredReferenceViews (juce::Graphics&, juce::Rectangle<float>,
                                    const State&);
}
