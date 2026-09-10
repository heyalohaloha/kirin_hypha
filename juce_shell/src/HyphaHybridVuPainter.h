#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaObservatoryContract.h"
#include "HyphaPresentationContext.h"
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
    bool recording = false;
    juce::String connectionText;
    juce::Colour connectionColour;
    presentation::Context presentation = presentation::defaultContext();
};

float vuNormalized (double dbfs) noexcept;
float truePeakNormalized (double dbtp) noexcept;
void paint (juce::Graphics&, juce::Rectangle<int>, const State&);
}
