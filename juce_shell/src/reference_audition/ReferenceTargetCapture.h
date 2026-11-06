#pragma once
#include "ReferenceMeasuredTarget.h"
#include "ReferenceVisualTimeline.h"
#include "ReferenceLiveWindowLoudness.h"

namespace hypha::reference_audition
{
// The worker isolates the K-filter state at the selected window's start. Continuous A bins
// cannot supply this value: their IIR state still contains audio outside the window.
inline LiveWindowLoudness targetWindowLevels (const VisualTimeline& timeline)
{
    if (! timeline.observing || ! timeline.aKirin || timeline.aKirin->frames < 30
        || timeline.aTargetLoudness.blocks != timeline.aKirin->frames) return {};
    auto result = timeline.aTargetLoudness;
    result.maxTruePeak = timeline.aTargetTruePeak;
    return result;
}
// Copy displayed A facts, including the worker's zero-extended TP window. Never retain PCM.
inline MeasuredTarget targetFromA (const VisualTimeline& timeline)
{
    const auto levels = targetWindowLevels (timeline);
    if (levels.blocks < 30) return {};
    MeasuredTarget result;
    result.id = "target-" + juce::Uuid().toString().removeCharacters ("-");
    result.name = "A " + juce::String (timeline.aKirin->frames / 10.0, 1) + " S";
    result.channels = timeline.aTickChannels;
    result.duration = timeline.aKirin->frames / 10.0;
    result.spectrum = *timeline.aKirin;
    result.integrated = levels.lufs;
    result.maximumTruePeak = levels.maxTruePeak;
    return result.valid() ? result : MeasuredTarget {};
}
}
