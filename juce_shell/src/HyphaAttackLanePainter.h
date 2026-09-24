#pragma once

#include <cstdint>
#include <initializer_list>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaAttackLaneModel.h"
#include "HyphaPresentationContext.h"

namespace hypha::attack_lane_painter
{
struct Frame
{
    const attack_lanes::Model& model;
    std::int64_t latest = 0;
    std::uint32_t rate = 0;
    std::int64_t selected = -1;
    presentation::Context context = presentation::defaultContext();
};

// Draws the first candidate that fits the area and returns false when none fits, so a narrow
// capture or resize never paints clipped or overlapping text. Tracking applies to labels only.
bool drawFitting (juce::Graphics&, std::initializer_list<juce::String> candidates,
                  juce::Rectangle<int>, const presentation::Context&, typography::TextRole,
                  juce::Justification, float tracking = 0.0f);

juce::Colour colourFor (attack_lanes::Lane) noexcept;
juce::String nameFor (attack_lanes::Lane);
juce::String codeFor (attack_lanes::Lane);
juce::String scaleCaption (attack_lanes::Lane, bool delta);
juce::String valueText (attack_lanes::Lane, float value, bool delta, bool withUnit);
juce::String reasonText (const attack_lanes::Hit&, attack_lanes::Reason);

// One lane row is split by lifetime. The chrome (name, fixed scale, stage, zero line) only
// changes with size, context and pairing and is cached; the values (per-hit filaments on the
// shared six-second axis and the selected hit's value or withheld reason) are painted per frame.
void paintLaneChrome (juce::Graphics&, attack_lanes::Lane, juce::Rectangle<int> label,
                      juce::Rectangle<int> plot, bool delta, const presentation::Context&);
void paintLaneValues (juce::Graphics&, attack_lanes::Lane, juce::Rectangle<int> plot,
                      juce::Rectangle<int> readout, const Frame&);
void paintLine (juce::Graphics&, juce::Rectangle<int>, const Frame&);
void paintHistoryLabel (juce::Graphics&, juce::Rectangle<int>, const presentation::Context&);
void paintSelectedTime (juce::Graphics&, juce::Rectangle<int>, const Frame&);

// The selected hit is one static hypha from HISTORY through every lane. Its drift is seeded by
// the hit and stays within one pixel of the exact event x; it never moves with time.
void paintHypha (juce::Graphics&, float x, float top, float bottom, float bulbY,
                 std::int64_t seed);
}
