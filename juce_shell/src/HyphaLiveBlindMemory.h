#pragma once

#include "HyphaBlindPreferenceTrial.h"
#include "live_compare/LiveCompareProcessorState.h"
#include <juce_core/juce_core.h>
#include <optional>

namespace hypha::live_blind_ui
{
// What one LIVE BLIND trial remembers besides the audio: the answers given, the levels approved before the
// first one and the notes RESULT states. The processor keeps it, so closing and reopening the window shows
// the same trial and the same RESULT. Message thread only; never saved with the project. Nothing here names
// a side before REVEAL: the screen reads it only from a revealed trial of this very generation and epoch.
struct TrialMemory
{
    blind_preference::Trial preference;
    juce::String preferenceNameA, preferenceNameB;
    int activeRounds = 0; // rounds of the trial held here; 0 holds none
    std::uint64_t generation = 0;
    std::uint32_t epoch = 0;
    bool awaitingEpoch = false;
    std::uint32_t expectedEpoch = 0;
    bool preferenceFallback = false, preferenceTimingFallback = false, preferenceCeilingFallback = false;
    double preferenceContentNoticeMs = 0;
    bool approvedGainsValid = false;
    std::optional<live_compare::GainSnapshot> approvedGainAtStart;
    double approvedPreDb = 0, approvedPostDb = 0;
    std::uint64_t approvedGainGeneration = 0;
    // The GUESS answer of the plain listen (0 before one is chosen) and the trial it belongs to.
    int answer = 0;
    std::uint64_t answerGeneration = 0;
    std::uint32_t answerEpoch = 0;

    // This trial, and no other: a new one, END, a restore and a changed pair or format all change them.
    bool holds (std::uint64_t trialGeneration, std::uint32_t trialEpoch) const noexcept
    {
        return activeRounds != 0 && generation == trialGeneration
            && trialEpoch == (preference.phase() == blind_preference::Phase::off ? epoch : preference.epoch());
    }
};
}
