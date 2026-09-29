#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackBandSummary.h"
#include "HyphaAttackLaneModel.h"
#include "HyphaAttackUiContract.h"
#include "HyphaPresentationContext.h"

// DRUM band summary painting (2026-09-29). While LIVE, the four lanes are number lines of the
// recent hits that rise in the band: each hit a dot at its value, their spread, the median as a
// bar, and the readout the median with its direction and how many hits agree. A card (200% and
// 300%) or a two-line reading (150%) says the result in words; at 125% the HISTORY row holds four
// small number lines with their words; 100% shows the medians. A locked hit keeps the number lines
// and rings its own dot; its readouts are its own values.
namespace hypha::attack_band_summary_painter
{
// Chrome (cached): a lane's number line: the well, zero for POST - PRE, ticks and the scale's ends.
void paintLaneChrome (juce::Graphics&, std::size_t lane, juce::Rectangle<int> plot, bool delta,
                      const presentation::Context&);
// Chrome: the axis row over the number lines: which side is less, zero, which side is more.
void paintAxisRow (juce::Graphics&, juce::Rectangle<int> row, juce::Rectangle<int> linePlot, bool delta,
                   const presentation::Context&);

// Geometry shared by painting and hit testing: where hit `index` of the summary stands in `plot`.
juce::Point<float> dotCentre (const KirinAttackBandSummary&, std::size_t lane, juce::Rectangle<int> plot,
                              std::size_t index) noexcept;
// The summed hit whose dot is under `point` in the lane, or -1.
int dotAt (const KirinAttackBandSummary&, std::size_t lane, juce::Rectangle<int> plot,
           juce::Point<int> point) noexcept;

// Per frame: the band's resolution, the hits' spread, each hit's dot and the median; `ringed` is
// the summed hit to ring (the newest while LIVE, the locked one), -1 for none.
void paintLaneValues (juce::Graphics&, std::size_t lane, juce::Rectangle<int> plot,
                      const KirinAttackBandSummary&, int ringed, const presentation::Context&);
// Per frame: a lane's readout: the median, its direction and how many hits agree, or why the lane
// has no value (`delayReason` for DELAY without PRE).
void paintReadout (juce::Graphics&, std::size_t lane, juce::Rectangle<int> cell,
                   const KirinAttackBandSummary&, const juce::String& delayReason, const presentation::Context&);
// Per frame: a locked hit's own value in the lane, or why it has none.
void paintHitReadout (juce::Graphics&, attack_lanes::Lane, juce::Rectangle<int> cell,
                      const attack_lanes::Hit*, bool delta, const presentation::Context&);

// The result in words: title, one line a fact and (when it fits) the hits left out. While nothing
// is summed, `waiting` (PLAY TO MEASURE or MEASURING) instead.
void paintCard (juce::Graphics&, juce::Rectangle<int> area, const KirinAttackBandSummary&,
                const juce::String& bandName, const juce::String& delayReason,
                const juce::String& waiting, const presentation::Context&);
// 150%: the title and the facts on one line each.
void paintReading (juce::Graphics&, juce::Rectangle<int> area, const KirinAttackBandSummary&,
                   const juce::String& bandName, const juce::String& delayReason,
                   const juce::String& waiting, const presentation::Context&);
// 125%: where each lane's small number line stands in `area` (the HISTORY row), shared by painting
// and hit testing; empty where the area is too small.
std::array<juce::Rectangle<int>, attack_band_summary::laneCount> rowPlots (juce::Rectangle<int> area,
                                                                          const presentation::Context&);
// 125%: the title, then each lane's code, number line and word in one row.
void paintRows (juce::Graphics&, juce::Rectangle<int> area, const KirinAttackBandSummary&, int ringed,
                const juce::String& bandName, const juce::String& delayReason, const juce::String& waiting,
                const presentation::Context&);
// 125%: the four medians in the one-line readout.
void paintLine (juce::Graphics&, const attack_ui::Layout&, const KirinAttackBandSummary&,
                const juce::String& delayReason, const presentation::Context&);
// 100%: the band and the count in HISTORY, the four medians large below.
void paintGlance (juce::Graphics&, const attack_ui::Layout&, const KirinAttackBandSummary&,
                  const juce::String& bandName, const juce::String& delayReason, const juce::String& waiting,
                  const presentation::Context&);

juce::String laneTooltip (std::size_t lane, bool delta);
juce::String cardTooltip();
}
