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
template<class Candidates, class Candidate>
juce::String menuLabel (const Candidates& candidates, const Candidate& candidate)
{

    int duplicates = 0;
    for (const auto& other : candidates)
        if (other.hasName && other.name == candidate.name) ++duplicates;
    auto label = candidate.hasName && candidate.name.isNotEmpty()
        ? candidate.name + (duplicates > 1 ? " / " + candidate.instanceId.substring (0, 8) : juce::String())
        : "PRE " + candidate.instanceId.substring (0, 8);
    if (candidate.hostName.isNotEmpty() && candidate.hostName != candidate.name) label += " / " + candidate.hostName;
    return candidate.chainPosition >= 0 ? "FX " + juce::String (candidate.chainPosition + 1) + ": " + label : label;
}
// Another POST's saved pair names this PRE, by identity or, for an older record, by its name.
template<class Candidate, class Claims>
bool claimedByOtherPost (const Candidate& candidate, const juce::String& ownInstanceId, const Claims& claims)
{
    for (const auto& claim : claims)
        if (claim.instanceId != ownInstanceId
            && ((claim.hasPairedPreInstanceId && claim.pairedPreInstanceId == candidate.instanceId)
                || (! claim.hasPairedPreInstanceId && claim.hasPairPreName && candidate.hasName
                    && candidate.name.isNotEmpty() && claim.pairPreName == candidate.name)))
            return true;
    return false;
}
inline juce::String pairedLabel (const juce::String& name)
{ return name.startsWith ("PRE ") ? name : juce::String (prefix) + name; }
}
