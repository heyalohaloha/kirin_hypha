#include "HyphaAttackBandPainter.h"

#include <initializer_list>

#include "HyphaAttackLanePainter.h"
#include "HyphaAttackStage.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// The band's words and choices: names and ranges, hover help, the chips, the label cell, and the
// guidance when nothing in view was measured in the chosen band.
namespace hypha::attack_band_painter
{
namespace
{
using attack_lanes::Lane;
using typography::TextRole;
constexpr auto visualization = typography::Composition::visualization;
const auto postColour = juce::Colour (attack_ui::waveformColour);

juce::Rectangle<int> rectangleOf (attack_ui::Box box)
{
    return { box.x, box.y, box.width, box.height };
}

int lineHeight (const presentation::Context& context, TextRole role)
{
    return text_style::requiredLineHeight (typography::resolve (context, role, visualization));
}

juce::String kilo (float hz)
{
    const auto k = hz / 1'000.0f;
    return juce::String (k, k < 10.0f ? 2 : 1);
}
}

juce::String nameText (std::uint8_t band)
{
    const auto* entry = attack_band::bandFor (band);
    if (entry == nullptr)
        return "ALL";
    const juce::String label (entry->label);
    return label.endsWithChar ('k') ? label.dropLastCharacters (1) + " kHz" : label + " Hz";
}

juce::String rangeText (std::uint8_t band)
{
    const auto* entry = attack_band::bandFor (band);
    if (entry == nullptr)
        return {};
    const auto low = entry->lowHz();
    const auto high = entry->highHz();
    if (high < 1'000.0f)
        return juce::String (juce::roundToInt (low)) + "-" + juce::String (juce::roundToInt (high)) + " Hz";
    if (low >= 1'000.0f)
        return kilo (low) + "-" + kilo (high) + " kHz";
    return juce::String (juce::roundToInt (low)) + " Hz-" + kilo (high) + " kHz";
}

juce::String playText (std::uint8_t band)
{
    return attack_band::bandFor (band) == nullptr ? juce::String ("PLAY TO MEASURE")
                                                  : "PLAY TO MEASURE " + nameText (band);
}

juce::String chipTooltip (std::uint8_t band)
{
    const auto* entry = attack_band::bandFor (band);
    if (entry == nullptr)
        return "ALL: every hit measured on the whole signal, as DRUM always has. No audio is kept for the bands while ALL is chosen.";
    return "Octave band " + nameText (band) + ": " + rangeText (band)
         + ". Each hit is measured in this band on PRE and POST once its ring-out is over; switching between bands measures the last 7 s again, even when stopped. Time resolution: one period, "
         + attack_lane_painter::resolutionText (entry->periodMs()) + ".";
}

juce::String predatesTooltip()
{
    return "This PRE predates bands and sends none: update PRE to compare the band. POST's own values are shown meanwhile.";
}

juce::String paneTooltip (bool head)
{
    return head ? "HEAD: the band envelope around the onset. The lines mark where PRE and POST rise through their peak - 20 dB; DELAY is the gap between them."
                : "TAIL: the band envelope over 300 ms. The marks show where PRE and POST fall to peak - 20 dB; REL is the time from the peak to that point.";
}

juce::String laneTooltip (Lane lane)
{
    switch (lane)
    {
        case Lane::delay:
            return "DELAY: POST arrival minus PRE arrival in this band, ms. Arrival is where the envelope rises through its peak - 20 dB. RINGING: the previous hit still rings in this band, so the start cannot be timed.";
        case Lane::attackTime:
            return "ATT: the rise from 10 % to 90 % of the band peak, ms. A rise shorter than the band's time resolution (one period) reads as an upper bound, such as <16 ms.";
        case Lane::release:
            return "REL: the fall from the band peak to peak - 20 dB, ms. >288 ms: still ringing where the measured tail ends. NEXT HIT: the next hit came first. LONG TAIL: both sides ring past the tail.";
        case Lane::level:
            return "LEVEL: the band's peak envelope level. POST - PRE in dB when paired, dBFS otherwise. When one side has no sound in the band, the difference reads as a bound, such as <-66 dB.";
        case Lane::transient:
        case Lane::strength:
        case Lane::crest:
        case Lane::sharpness:
            return {};
    }
    return {};
}

std::array<juce::String, 3> legend (std::uint8_t band, bool paired)
{
    const auto range = rangeText (band);
    if (paired)
        return { range + "   PRE trace / POST body   bars POST - PRE", range + " / bars POST-PRE", range };
    return { range + "   POST body   bars POST values", range + " / POST values", range };
}

void paintChips (juce::Graphics& g, const attack_ui::Layout& layout,
                 const presentation::Context& context, std::uint8_t band)
{
    const auto caption = rectangleOf (attack_band::chipCaption (layout, context));
    if (caption.isEmpty())
        return;
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (context, TextRole::legend, visualization)
                   .withExtraKerningFactor (attack_stage::captionTracking (context)));
    text_style::drawText (g, "BAND", caption.withTrimmedLeft (4), juce::Justification::centredLeft, false);
    for (std::size_t choice = 0; choice < attack_band::choiceCount; ++choice)
    {
        const auto cell = rectangleOf (attack_band::chipCell (layout, context, choice));
        const bool selected = choice == band;
        surface_material::paintControl (g, cell.reduced (2, 1).toFloat(), false, false, selected,
                                        postColour, 3.0f);
        g.setColour (selected ? COL_NORMAL : COL_TEXT_SECONDARY);
        g.setFont (monoFont (context, TextRole::legend, visualization));
        text_style::drawText (g, attack_band::labelFor (static_cast<std::uint8_t> (choice)), cell,
                              juce::Justification::centred, false);
    }
}

void paintPaneLabel (juce::Graphics& g, juce::Rectangle<int> area,
                     const presentation::Context& context, std::uint8_t band)
{
    auto cell = area.reduced (4, 2);
    const auto nameHeight = lineHeight (context, TextRole::metricLabel);
    const auto captionHeight = lineHeight (context, TextRole::legend);
    if (cell.getHeight() < nameHeight)
        return;
    cell = cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(),
                                                                   nameHeight + captionHeight));
    g.setColour (COL_TEXT_SECONDARY);
    attack_lane_painter::drawFitting (g, { nameText (band), attack_band::labelFor (band) },
                                      cell.removeFromTop (nameHeight), context, TextRole::metricLabel,
                                      juce::Justification::centredLeft,
                                      attack_stage::labelTracking (context));
    g.setColour (COL_TEXT_TERTIARY);
    attack_lane_painter::drawFitting (g, { "HIT", "" }, cell, context, TextRole::legend,
                                      juce::Justification::centredLeft,
                                      attack_stage::captionTracking (context));
}

void paintPlayGuidance (juce::Graphics& g, juce::Rectangle<int> history,
                        const presentation::Context& context, std::uint8_t band)
{
    if (band == 0 || history.isEmpty())
        return;
    g.setColour (COL_TEXT_SECONDARY);
    attack_lane_painter::drawFitting (g, { playText (band), playText (0) }, history.reduced (8, 4),
                                      context, TextRole::status, juce::Justification::topLeft);
}

void paintGlanceCaption (juce::Graphics& g, juce::Rectangle<int> history,
                         const presentation::Context& context, std::uint8_t band, bool needsPlay)
{
    if (band == 0 || history.isEmpty())
        return;
    g.setColour (COL_TEXT_SECONDARY);
    attack_lane_painter::drawFitting (g, needsPlay ? std::initializer_list<juce::String> {
                                                         playText (band), playText (0), nameText (band) }
                                                   : std::initializer_list<juce::String> { nameText (band) },
                                      history.reduced (6, 3), context, TextRole::legend,
                                      juce::Justification::topLeft);
}
}
