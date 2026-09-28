#pragma once

#include <array>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackBandContract.h"
#include "HyphaAttackLaneModel.h"
#include "HyphaPresentationContext.h"

// DRUM BAND painting (B-1097): the chips that choose the band, the HEAD / TAIL panes of the
// selected hit, and the band's own text. Labels and legends stay English; the hover help is
// prose and has Japanese (INV-S40).
namespace hypha::attack_band_painter
{
juce::String nameText (std::uint8_t band);   // "63 Hz", "2 kHz"
juce::String rangeText (std::uint8_t band);  // "44-89 Hz", "1.41-2.83 kHz"
juce::String chipTooltip (std::uint8_t band);
juce::String pendingTooltip();
juce::String paneTooltip (bool head);
juce::String laneTooltip (attack_lanes::Lane);
// The header legend while a band is chosen, longest first.
std::array<juce::String, 3> legend (std::uint8_t band, bool paired);

// Chrome (cached): "BAND" and the nine chips, the chosen one lit.
void paintChips (juce::Graphics&, const attack_ui::Layout&, const presentation::Context&,
                 std::uint8_t band);
// Chrome: the band's name and HIT in the HISTORY label cell.
void paintPaneLabel (juce::Graphics&, juce::Rectangle<int>, const presentation::Context&,
                     std::uint8_t band);
// Chrome: the two wells, their captions and time labels, the dB grid. `prePending` names the
// PRE that has not sent the band where PRE / POST would stand.
void paintPaneChrome (juce::Graphics&, const attack_ui::Layout&, const presentation::Context&,
                      bool twoRows, bool paired, bool prePending);
// Per frame: the selected hit's PRE trace and POST body in the band, the arrival and release
// marks and the DELAY and REL brackets; NO HIT without a selection.
void paintPanes (juce::Graphics&, const attack_ui::Layout&, const presentation::Context&,
                 const KirinAttackBandHit*, bool twoRows);
// 100%: the chosen band named in HISTORY, where only a non-default choice is named (INV-S38).
void paintGlanceCaption (juce::Graphics&, juce::Rectangle<int> history,
                         const presentation::Context&, std::uint8_t band);
}
