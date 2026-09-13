#pragma once
#include "ReferenceRuntimeV2Blind.h"

namespace hypha::reference_audition
{
    juce::var wholeSongTrialStart (juce::var legacy, const RuntimeCandidate&,
                                   const RuntimeV2BlindSnapshot&);
    juce::var wholeSongTrialCompleted (juce::var legacy);
}
