#pragma once

#include <array>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackBandContract.h"
#include "HyphaAttackBandModel.h"
#include "HyphaAttackLaneModel.h"
#include "HyphaPresentationContext.h"

// DRUM BAND painting (B-1097, B-1098): the chips that choose the band, the HEAD / TAIL panes of
// the selected hit, the band's own text, and what to do when nothing is measured yet. Labels and
// legends stay English; statuses, guidance and hover help have Japanese (INV-S40).
namespace hypha::attack_band_painter
{
juce::String nameText (std::uint8_t band);   // "63 Hz", "2 kHz"
juce::String rangeText (std::uint8_t band);  // "44-88 Hz", "1.41-2.83 kHz"
juce::String playText (std::uint8_t band);   // "PLAY TO MEASURE 63 Hz"
juce::String chipTooltip (std::uint8_t band);
juce::String predatesTooltip();
juce::String paneTooltip (bool head);
juce::String laneTooltip (attack_lanes::Lane);
// The header legend while a band is chosen, longest first.
std::array<juce::String, 3> legend (std::uint8_t band, bool paired);

// Chrome (cached): "BAND" and the nine chips, the chosen one lit.
void paintChips (juce::Graphics&, const attack_ui::Layout&, const presentation::Context&,
                 std::uint8_t band);
// Chrome: the band's name and what the row shows (HIT, AVERAGE, SUMMARY) in the HISTORY label cell.
void paintPaneLabel (juce::Graphics&, juce::Rectangle<int>, const presentation::Context&,
                     std::uint8_t band, const juce::String& caption);
// Chrome: the two wells, their captions and time labels, the dB grid. A PRE that predates
// bands is named where PRE / POST would stand.
void paintPaneChrome (juce::Graphics&, const attack_ui::Layout&, const presentation::Context&,
                      bool twoRows, bool delta, attack_band::PreBand);

struct PaneFrame
{
    // The selected hit's record with its envelopes, when it was fetched for that hit.
    const KirinAttackBandHitEnvelope* hit = nullptr;
    bool selected = false;
    // Nothing in the six seconds was measured in this band: play to measure.
    bool needsPlay = false;
    bool delta = false;
    bool twoRows = false;
    std::uint8_t band = 0;
};

// Per frame: the selected hit's PRE trace and POST body in the band, the arrival and release
// marks and the DELAY and REL brackets; otherwise one stated message in TAIL: NO HIT, MEASURING,
// PLAY TO MEASURE, NO PAIR or NO SOUND.
void paintPanes (juce::Graphics&, const attack_ui::Layout&, const presentation::Context&,
                 const PaneFrame&);
// While LIVE with a band: the average envelopes of the summed hits (PRE trace, POST body), POST's
// spread, the median arrival and release marks and the lanes' median DELAY and REL; `waiting`
// (PLAY TO MEASURE or MEASURING) in TAIL while nothing is summed.
void paintSummaryPanes (juce::Graphics&, const attack_ui::Layout&, const presentation::Context&,
                        const KirinAttackBandSummary&, const juce::String& waiting, bool twoRows);
// Below 200%: the same guidance over the six seconds.
void paintPlayGuidance (juce::Graphics&, juce::Rectangle<int> history,
                        const presentation::Context&, std::uint8_t band);
// 100%: the chosen band named in HISTORY, where only a non-default choice is named (INV-S38),
// or the guidance when nothing was measured in it.
void paintGlanceCaption (juce::Graphics&, juce::Rectangle<int> history,
                         const presentation::Context&, std::uint8_t band, bool needsPlay);
}
