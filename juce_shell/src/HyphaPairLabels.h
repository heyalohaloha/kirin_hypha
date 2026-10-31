#pragma once
#include <juce_core/juce_core.h>

namespace hypha::pair_labels
{
inline constexpr auto prefix = "PRE  ";
inline constexpr auto unpaired = "NO PRE";
inline constexpr auto choose = "Choose PRE";
inline constexpr auto locked = "Locked during playback";
inline constexpr auto changed = "PRE changed";
inline constexpr auto candidatePrefix = "+ ";
inline constexpr auto candidateShort = "+ PRE"; // a candidate whose name does not fit the badge
inline constexpr auto menuPrefix = "PRE: ";
inline constexpr auto candidateHelp = u8"Click to connect\n▼ All PREs";
inline juce::String candidateLabel (const juce::String& name, const juce::String& id)
{ return juce::String (candidatePrefix) + (name.isNotEmpty() ? name : id.substring (0, 8)); }
inline juce::String candidateTooltip (const juce::String& name, const juce::String& id)
{ return (name.isNotEmpty() ? name + " / " : juce::String()) + id + "\n" + juce::String::fromUTF8 (candidateHelp); }
inline juce::String pairedLabel (const juce::String& name)
{ return name.startsWith ("PRE ") ? name : juce::String (prefix) + name; }
}
