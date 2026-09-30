#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

// The REFERENCE page's guide (INV-S41). B and C can be heard only while the DAW plays and once
// Kirin OS has delivered them; until then the page used to show greyed buttons and empty values
// with no reason. When neither can be heard, the guide takes the comparison's place and says the
// one next step, what A, B and C are, and where each of them stands. A B or C that cannot be heard
// yet explains itself on hover and on a click instead of doing nothing.
namespace hypha::reference_ui
{
struct State;

// What one of B and C still needs before it can be heard.
enum class SourceStep
{
    ready,
    waitingForKirinOs,
    registerVersion,
    chooseVersion,
    enableCheck,
    chooseSource,
    playDaw,
    aligning,
    noMatchingPassage,
    playAnotherPassage,
    approveSampleRate,
    verifyingSource,
    loadingAudio,
    outsideCue,
    preparing,
    attention,
};

struct Guide
{
    bool shown = false;
    juce::String heading, detail;
    SourceStep version = SourceStep::ready, check = SourceStep::ready;
    bool playing = false;
};

// B's and C's steps as the page states them: one that the button cannot deliver is not ready.
Guide guide (const State&);
juce::String stepText (SourceStep);
// "B: Choose a Version": the reason shown on hover and after a click on a B or C not ready yet.
juce::String unavailableText (const State&, bool version);

// What paintGuide showed whole: the heading, the reason, and the rows it drew (none, B and C, or
// A, B and C). Text that does not fit is cut with an ellipsis, never compressed.
struct GuideFit
{
    bool heading = true, detail = true, rows = true;
    int rowCount = 0;
};
GuideFit paintGuide (juce::Graphics&, juce::Rectangle<int>, const Guide&, presentation::Context);
}
