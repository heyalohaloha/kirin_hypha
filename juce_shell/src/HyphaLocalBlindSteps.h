#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "local_blind/LocalBlindProductSession.h"

#include <array>

// PRE / POST Blind as five steps with the current one lit (INV-S43). Each screen used to show only
// its own instruction, so the test as a whole, and how far along it was, could not be read.
namespace hypha::local_blind_ui
{
enum class Step
{
    capture,
    start,
    listen,
    answer,
    result,
    none,
};

// Returning to the live signal is outside the test: no step is lit then.
Step stepFor (const local_blind::ProductSessionView&) noexcept;

// The five cells of a steps area from left to right, the font the names are drawn in, and each
// name as written in the source; the screen shows it in the current language (HyphaTextStyle.h).
std::array<juce::Rectangle<int>, 5> stepCells (juce::Rectangle<int> area) noexcept;
juce::Font stepFont (presentation::Context);
// The row height that holds a name above its underline at this size.
int stepsHeight (presentation::Context);
juce::String stepName (int index);
void paintSteps (juce::Graphics&, juce::Rectangle<int>, Step, presentation::Context);

// What the test is, said once on the screen that starts it.
juce::String purposeText();
}
