#pragma once

#include "pre_display/PreDisplayModel.h"

// The facts the OS guide's information menu lists, in order. A MASKING guide lists the measured band
// and, on its own line, the frequency focus the user chose in Kirin OS (the FREQ overlay draws both;
// the menu used to give the measured band only). A range that was not chosen is not listed.
namespace hypha::guide_information
{
inline juce::String bandText (const pre_display::GuidePresentationFact& fact)
{
    return juce::String (fact.lowHz, 0) + " to " + juce::String (fact.highHz, 0) + " Hz";
}

inline juce::StringArray facts (const pre_display::DisplaySnapshot& display,
                                const pre_display::GuidePresentationSnapshot& guide)
{
    juce::StringArray out;
    const auto add = [&out] (const juce::String& text) { if (text.isNotEmpty()) out.add (text); };
    add (display.primary);
    add (display.detail);
    add (display.stateText);
    add (guide.sourcePairLabel.isNotEmpty() ? "Sources  " + guide.sourcePairLabel : juce::String());
    if (guide.hasPrimary)
    {
        add (guide.primary.label);
        add (guide.primary.sourceLabel.isNotEmpty() ? "Source  " + guide.primary.sourceLabel : juce::String());
        add (guide.primary.channel.isNotEmpty() ? "Channel  " + guide.primary.channel : juce::String());
        if (guide.primary.hasBand)
            add (bandText (guide.primary)
                 + (guide.primary.frequencyBasis.isNotEmpty() ? " / " + guide.primary.frequencyBasis : juce::String()));
    }
    // The playing item's chosen range, or before it starts the next item's.
    const auto& focus = guide.hasMaskingFocus ? guide.maskingFocus : guide.nextMaskingFocus;
    if (guide.payloadKind == "masking" && (guide.hasMaskingFocus || guide.hasNextMaskingFocus) && focus.hasBand)
        add ("Frequency focus  " + bandText (focus));
    return out;
}
}
