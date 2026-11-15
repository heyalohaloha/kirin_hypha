#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "reference_audition/ReferenceVisualTimeline.h"

// V compares A and the aligned Version over the same section. The main view is tone;
// DETAILS offers the timeline and individual observations without changing audition.
// V's choices are independent of C's CHECK SET. A and V use the same measurement frames;
// V is shown at its audition gain, or at the alignment gain before it is heard.
namespace hypha::reference_ui
{
struct VersionTab
{
    juce::String id, label;
    std::vector<juce::String> views;  // comparison bindings, including individual V detail views
};
const std::vector<VersionTab>& versionTabs();
const VersionTab* findVersionTab (const juce::String& id);
bool sameSectionReady (const reference_audition::VisualTimeline*) noexcept;
void paintVersionSameSection (juce::Graphics&, juce::Rectangle<int>, const reference_audition::VisualTimeline*,
                              const juce::String& checkLabel, double gainDb, const std::vector<juce::String>& views,
                              bool listening, presentation::Context, juce::Point<float> pointer = { -1.0f, -1.0f }, bool primaryOnly = false);
// 耳で聴き比べる Check の案内（V と C の画面。2026-10-04、図の代わりに。Tone と同じ図を出さない）。
juce::String listeningGuide (char role);
}
