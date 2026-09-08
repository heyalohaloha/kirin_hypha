#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaObservatoryContract.h"
#include "kirin_hypha_ffi.h"

namespace hypha::hybrid_vu
{
constexpr double referenceDbfs = -18.0;

struct State
{
    observatory::Role role;
    const KirinMeterSession& meter;
    const KirinWatchDisplay& watch;
    bool currentAvailable = false;
    bool cumulativeAvailable = false;
    bool watchAvailable = false;
    bool shortTermLoudness = false;
    juce::String connectionText;
    juce::Colour connectionColour;
};

float vuNormalized (double dbfs) noexcept;
float truePeakNormalized (double dbtp) noexcept;
void paint (juce::Graphics&, juce::Rectangle<int>, const State&);
}
