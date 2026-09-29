#pragma once

#include <cstddef>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaAttackBandSummary.h"
#include "HyphaAttackLaneModel.h"
#include "HyphaAttackLanePainter.h"
#include "HyphaAttackStage.h"
#include "HyphaPresentationContext.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// What the DRUM band summary's painters share (HyphaAttackBandSummaryPainter.cpp draws the number
// lines, HyphaAttackBandSummaryWords.cpp the words): each lane's colour and the text faces.
namespace hypha::attack_band_summary_painter::style
{
using typography::TextRole;
constexpr auto visualization = typography::Composition::visualization;

inline juce::Colour colourOf (std::size_t lane)
{
    return attack_lane_painter::colourFor (attack_lanes::bandLanes[lane < attack_band_summary::laneCount ? lane : 0]);
}

inline int lineHeight (const presentation::Context& context, TextRole role)
{
    return text_style::requiredLineHeight (typography::resolve (context, role, visualization));
}

inline juce::Font font (const presentation::Context& context, TextRole role)
{
    return monoFont (context, role, visualization);
}

// Whether `text` fits `width` in `role`, measured as drawFitting measures it.
inline bool fits (const presentation::Context& context, TextRole role, const juce::String& text, int width)
{
    return text_style::requiredWidth (attack_stage::trackedFont (context, role, 0.0f), text,
                                      typography::resolve (context, role, visualization))
        <= width;
}

inline void text (juce::Graphics& g, const juce::String& shown, juce::Rectangle<int> area,
                  const presentation::Context& context, TextRole role, juce::Colour colour,
                  juce::Justification justification)
{
    g.setColour (colour);
    g.setFont (monoFont (context, role, visualization));
    text_style::drawText (g, shown, area, justification, false);
}
}
