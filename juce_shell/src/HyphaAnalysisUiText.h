#pragma once

#include <juce_core/juce_core.h>

namespace hypha::analysis_ui
{
inline juce::String switchViewTooltip (const char* viewName)
{
    return juce::String (viewName) + ". Click to switch view.";
}

inline juce::String slotsInUse (const juce::String& ownerNames)
{
    auto text = juce::String ("Both slots in use");
    if (ownerNames.isNotEmpty())
        text += " " + juce::String::charToString (0x2014) + " " + ownerNames;
    return text;
}
}
