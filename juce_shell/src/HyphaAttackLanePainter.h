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
    // The selected hit while it is on the six-second axis. A locked hit that has scrolled out has
    // no hypha, so its values are not shown either.
    const attack_lanes::Hit* selected = nullptr;
    presentation::Context context = presentation::defaultContext();
    // The four rows: the whole-signal lanes, or the band lanes while a band is chosen.
    const std::array<attack_lanes::Lane, attack_ui::laneCount>& lanes = attack_lanes::lanes;
};

// Draws the first candidate that fits the area and returns false when none fits, so a narrow
// capture or resize never paints clipped or overlapping text. Tracking applies to labels only.
bool drawFitting (juce::Graphics&, std::initializer_list<juce::String> candidates,
                  juce::Rectangle<int>, const presentation::Context&, typography::TextRole,
                  juce::Justification, float tracking = 0.0f);

// A short lane-colour mark identifies a readout; the number itself stays ivory.
void paintAccent (juce::Graphics&, juce::Rectangle<int> row, juce::Colour, float alpha);

juce::Colour colourFor (attack_lanes::Lane) noexcept;
juce::String nameFor (attack_lanes::Lane);
juce::String codeFor (attack_lanes::Lane);
juce::String scaleCaption (attack_lanes::Lane, bool delta);
juce::String unitFor (attack_lanes::Lane, bool delta);
juce::String valueText (attack_lanes::Lane, float value, bool delta, bool withUnit);
// A band's time resolution, one period of its centre: "16 ms", "0.13 ms"; and the ATT bound
// stated inside it (D4): "<16 ms".
juce::String resolutionText (float ms);
juce::String boundText (float ms);
juce::String reasonText (const attack_lanes::Hit&, attack_lanes::Reason);
// The reason in a cell too narrow for it: the 100% glance and the smallest readouts.
juce::String shortReasonText (const attack_lanes::Hit&, attack_lanes::Reason);
// What a cell says: its value, a bound ("<16 ms", ">288 ms", "<-66.0 dB"), or why it is withheld.
juce::String cellText (const attack_lanes::Hit&, attack_lanes::Lane, bool delta, bool withUnit);
// A cell that states a fact (a value or a bound) is drawn as a value; a reason is not.
inline bool stated (const attack_lanes::Cell& cell) noexcept
{
    return cell.reason == attack_lanes::Reason::value
        || cell.reason == attack_lanes::Reason::withinResolution
        || cell.reason == attack_lanes::Reason::atLeast;
}

// One lane row is split by lifetime. The chrome (name, fixed scale, stage, zero line) only
// changes with size, context and pairing and is cached; the values (per-hit filaments on the
// shared six-second axis and the selected hit's value or withheld reason) are painted per frame.
void paintLaneChrome (juce::Graphics&, attack_lanes::Lane, juce::Rectangle<int> label,
                      juce::Rectangle<int> plot, bool delta, const presentation::Context&);
void paintLaneValues (juce::Graphics&, attack_lanes::Lane, juce::Rectangle<int> plot,
                      juce::Rectangle<int> readout, const Frame&);
void paintLine (juce::Graphics&, const attack_ui::Layout&, const Frame&);
// 100%: the selected hit's four values, large, under their lane codes (HyphaAttackGlancePainter.cpp).
void paintGlance (juce::Graphics&, const attack_ui::Layout&, const Frame&);
void paintHistoryLabel (juce::Graphics&, juce::Rectangle<int>, const presentation::Context&);
void paintSelectedTime (juce::Graphics&, juce::Rectangle<int>, const Frame&);

// The selected hit is one static hypha from HISTORY through every lane. Its drift is seeded by
// the hit and stays within one pixel of the exact event x; it never moves with time.
void paintHypha (juce::Graphics&, float x, float top, float bottom, float bulbY,
                 std::int64_t seed);
}
