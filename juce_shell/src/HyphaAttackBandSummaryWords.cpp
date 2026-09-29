#include "HyphaAttackBandSummaryPainter.h"

#include <cmath>
#include <initializer_list>
#include <vector>

#include "HyphaAttackBandSummaryStyle.h"

// The summary in words: the card (200%, 300%), the reading (150%), the rows (125%), the one-line
// readout (125%) and the glance (100%), and the hover help. Each says the band, how many hits it
// sums, what moved first and what did not after, shortening itself rather than clipping.
namespace hypha::attack_band_summary_painter
{
namespace
{
using attack_band_summary::Fact;
using attack_band_summary::laneCount;
using attack_band_summary::laneOf;
using namespace style;

juce::String joined (const std::vector<Fact>& facts, std::size_t count, bool brief)
{
    juce::String result;
    for (std::size_t index = 0; index < facts.size() && index < count; ++index)
        result += (index == 0 ? "" : "  /  ") + (brief ? facts[index].brief : facts[index].text);
    return result;
}

// The title, and the hits left out after it in a quieter face where both fit.
void paintTitle (juce::Graphics& g, juce::Rectangle<int> row, const KirinAttackBandSummary& summary,
                 const juce::String& bandName, const presentation::Context& context)
{
    const auto title = attack_band_summary::titleText (bandName, summary.count);
    g.setColour (COL_TEXT_SECONDARY);
    if (! attack_lane_painter::drawFitting (g, { title }, row, context, TextRole::legend, juce::Justification::centredLeft))
    {
        attack_lane_painter::drawFitting (g, { summary.count > 0 ? attack_band_summary::lastText (summary.count) : bandName,
                                               bandName },
                                          row, context, TextRole::legend, juce::Justification::centredLeft);
        return;
    }
    if (summary.count == 0 || summary.left_out == 0)
        return;
    const auto used = static_cast<int> (std::ceil (text_style::shownWidth (font (context, TextRole::legend), title))) + 16;
    g.setColour (COL_TEXT_TERTIARY);
    attack_lane_painter::drawFitting (g, { attack_band_summary::leftOutText (bandName, summary.left_out) },
                                      row.withTrimmedLeft (used), context, TextRole::legend,
                                      juce::Justification::centredLeft);
}

// What to do or wait for while nothing is summed: on one line where it fits, else on up to three.
void paintWaiting (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& waiting,
                   const presentation::Context& context)
{
    g.setColour (COL_TEXT_SECONDARY);
    for (const auto role : { TextRole::readout, TextRole::legend })
        if (attack_lane_painter::drawFitting (g, { waiting }, area, context, role, juce::Justification::centred))
            return;
    g.setFont (monoFont (context, TextRole::legend, visualization));
    text_style::drawLines (g, waiting, area, juce::Justification::centred, 3);
}

// A fact in the first way that fits: the sentence at `role`, then at the legend size, then its
// brief form and its bare value at each.
void paintFact (juce::Graphics& g, const Fact& fact, juce::Rectangle<int> row, TextRole role,
                const presentation::Context& context)
{
    for (const auto* shown : { &fact.text, &fact.brief, &fact.bare })
        for (const auto face : { role, TextRole::legend })
            if (attack_lane_painter::drawFitting (g, { *shown }, row, context, face, juce::Justification::centredLeft))
                return;
}
}

void paintCard (juce::Graphics& g, juce::Rectangle<int> area, const KirinAttackBandSummary& summary,
                const juce::String& bandName, const juce::String& delayReason, const juce::String& waiting,
                const presentation::Context& context)
{
    if (area.isEmpty())
        return;
    auto card = area.reduced (4, 3);
    attack_stage::paint (g, card.toFloat(), 4.0f, 0.22f, true);
    auto inner = card.reduced (8, 4);
    const auto titleLine = lineHeight (context, TextRole::legend);
    paintTitle (g, inner.removeFromTop (titleLine), summary, bandName, context);
    if (summary.count == 0)
    {
        paintWaiting (g, inner.withTrimmedTop (4), waiting, context);
        return;
    }
    const auto facts = attack_band_summary::cardFacts (summary, delayReason);
    // Every fact at the readout size when all of them fit, else as many as fit at the legend
    // size, the lanes that moved first.
    const auto count = static_cast<int> (facts.size());
    auto role = TextRole::readout;
    auto factLine = lineHeight (context, role) + 3;
    if (factLine * count + 4 > inner.getHeight())
    {
        role = TextRole::legend;
        factLine = lineHeight (context, role);
        inner.removeFromTop (2);
    }
    else
        inner.removeFromTop (4);
    for (const auto& fact : facts)
    {
        if (inner.getHeight() < factLine)
            break;
        auto row = inner.removeFromTop (factLine);
        // Each fact is led by its lane's colour: the lane the sentence is about.
        g.setColour (colourOf (fact.lane).withAlpha (fact.quiet ? 0.45f : 0.95f));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ static_cast<float> (row.getX()) + 3.0f,
                                                                         static_cast<float> (row.getCentreY()) }));
        g.setColour (fact.quiet ? COL_TEXT_SECONDARY : COL_OBSERVATORY_VALUE);
        paintFact (g, fact, row.withTrimmedLeft (12), role, context);
    }
}

void paintReading (juce::Graphics& g, juce::Rectangle<int> area, const KirinAttackBandSummary& summary,
                   const juce::String& bandName, const juce::String& delayReason, const juce::String& waiting,
                   const presentation::Context& context)
{
    if (area.isEmpty())
        return;
    auto card = area.reduced (3, 2);
    attack_stage::paint (g, card.toFloat(), 4.0f, 0.22f, true);
    auto inner = card.reduced (8, 3);
    paintTitle (g, inner.removeFromTop (lineHeight (context, TextRole::legend)), summary, bandName, context);
    if (summary.count == 0)
    {
        g.setColour (COL_TEXT_SECONDARY);
        attack_lane_painter::drawFitting (g, { waiting }, inner, context, TextRole::readout,
                                          juce::Justification::centredLeft);
        return;
    }
    // As many facts as fit on the line, briefly before any is dropped.
    const auto facts = attack_band_summary::cardFacts (summary, delayReason);
    g.setColour (COL_OBSERVATORY_VALUE);
    for (auto count = facts.size(); count > 0; --count)
        for (const bool brief : { false, true })
            if (attack_lane_painter::drawFitting (g, { joined (facts, count, brief) }, inner, context,
                                                  TextRole::readout, juce::Justification::centredLeft))
                return;
}

void paintRows (juce::Graphics& g, juce::Rectangle<int> area, const KirinAttackBandSummary& summary, int ringed,
                const juce::String& bandName, const juce::String& delayReason, const juce::String& waiting,
                const presentation::Context& context)
{
    if (area.isEmpty())
        return;
    attack_stage::paint (g, area.reduced (3, 2).toFloat(), 4.0f, 0.22f, true);
    auto inner = area.reduced (8, 3);
    const auto titleRow = inner.removeFromTop (lineHeight (context, TextRole::legend) + 2);
    paintTitle (g, titleRow, summary, bandName, context);
    if (summary.count == 0)
    {
        paintWaiting (g, inner, waiting, context);
        return;
    }
    const auto plots = rowPlots (area, context);
    const bool delta = attack_band_summary::delta (summary);
    for (std::size_t lane = 0; lane < laneCount; ++lane)
    {
        const auto plot = plots[lane];
        if (plot.isEmpty())
            return;
        const auto code = juce::Rectangle<int>::leftTopRightBottom (inner.getX(), plot.getY(), plot.getX(), plot.getBottom());
        const auto words = juce::Rectangle<int>::leftTopRightBottom (plot.getRight() + 6, plot.getY(), inner.getRight(),
                                                                     plot.getBottom());
        g.setColour (colourOf (lane));
        attack_lane_painter::drawFitting (g, { attack_lane_painter::codeFor (attack_lanes::bandLanes[lane]) }, code,
                                          context, TextRole::legend, juce::Justification::centredLeft);
        paintLaneChrome (g, lane, plot, delta, context);
        paintLaneValues (g, lane, plot, summary, ringed, context);
        // The direction in words (the value and the agreement are in the line below), or why the
        // lane has no value.
        const auto& entry = laneOf (summary, lane);
        const bool stated = entry.state == KIRIN_ATTACK_BAND_LANE_VALUE;
        const auto word = entry.state == KIRIN_ATTACK_BAND_LANE_NONE
            ? attack_band_summary::withheldText (summary, lane, delayReason)
            : attack_band_summary::wordText (summary, lane);
        g.setColour (stated ? colourOf (lane) : COL_TEXT_TERTIARY);
        attack_lane_painter::drawFitting (g, { word, entry.state == KIRIN_ATTACK_BAND_LANE_WITHIN ? "SAME" : "" },
                                          words, context, TextRole::legend, juce::Justification::centredLeft);
    }
}

void paintLine (juce::Graphics& g, const attack_ui::Layout& layout, const KirinAttackBandSummary& summary,
                const juce::String& delayReason, const presentation::Context& context)
{
    for (std::size_t lane = 0; lane < laneCount; ++lane)
    {
        const auto box = attack_ui::lineCell (layout, lane);
        juce::Rectangle<int> cell (box.x, box.y, box.width, box.height);
        const bool stated = laneOf (summary, lane).state == KIRIN_ATTACK_BAND_LANE_VALUE;
        attack_lane_painter::paintAccent (g, cell, colourOf (lane), stated ? 0.9f : 0.35f);
        cell.removeFromLeft (attack_ui::lineAccentWidth);
        const auto code = attack_lane_painter::codeFor (attack_lanes::bandLanes[lane]);
        if (laneOf (summary, lane).state == KIRIN_ATTACK_BAND_LANE_NONE)
        {
            // Why the lane has no value, after its code: drawn apart so the reason keeps its
            // Japanese; where it does not fit, the code and "--".
            const auto codeWidth = juce::roundToInt (std::ceil (text_style::shownWidth (
                font (context, TextRole::readout), code + " ")));
            const auto why = attack_band_summary::withheldText (summary, lane, delayReason);
            g.setColour (COL_TEXT_SECONDARY);
            const bool said = why != "--"
                && attack_lane_painter::drawFitting (g, { why }, cell.withTrimmedLeft (codeWidth), context,
                                                     TextRole::readout, juce::Justification::centredLeft);
            g.setColour (COL_TEXT_TERTIARY);
            attack_lane_painter::drawFitting (g, { said ? code : code + " --", "--" }, cell, context, TextRole::readout,
                                              juce::Justification::centredLeft);
            continue;
        }
        const auto value = attack_band_summary::valueText (summary, lane);
        const auto agree = attack_band_summary::agreeText (summary, lane);
        g.setColour (stated ? COL_OBSERVATORY_VALUE : COL_TEXT_SECONDARY);
        attack_lane_painter::drawFitting (g, { code + " " + value + (agree.isEmpty() ? "" : "  " + agree), code + " " + value,
                                               code + " " + attack_band_summary::valueText (summary, lane, false) },
                                          cell, context, TextRole::readout, juce::Justification::centredLeft);
    }
}

void paintGlance (juce::Graphics& g, const attack_ui::Layout& layout, const KirinAttackBandSummary& summary,
                  const juce::String& bandName, const juce::String& delayReason, const juce::String& waiting,
                  const presentation::Context& context)
{
    const juce::Rectangle<int> history (layout.history.x, layout.history.y, layout.history.width, layout.history.height);
    g.setColour (COL_TEXT_SECONDARY);
    attack_lane_painter::drawFitting (g, { summary.count > 0 ? attack_band_summary::titleText (bandName, summary.count) : waiting,
                                           bandName },
                                      history.reduced (6, 3), context, TextRole::legend, juce::Justification::topLeft);
    const auto labelLine = lineHeight (context, TextRole::metricLabel);
    for (std::size_t lane = 0; lane < laneCount; ++lane)
    {
        const auto box = attack_ui::lineCell (layout, lane);
        juce::Rectangle<int> cell (box.x, box.y, box.width, box.height);
        const auto& entry = laneOf (summary, lane);
        const bool stated = entry.state == KIRIN_ATTACK_BAND_LANE_VALUE;
        attack_lane_painter::paintAccent (g, cell, colourOf (lane), stated ? 0.9f : 0.35f);
        cell.removeFromLeft (attack_ui::lineAccentWidth);
        auto head = cell.removeFromTop (labelLine);
        g.setColour (colourOf (lane));
        attack_lane_painter::drawFitting (g, { (attack_band_summary::delta (summary) ? hypha::delta() : juce::String())
                                               + attack_lane_painter::codeFor (attack_lanes::bandLanes[lane]) },
                                          head, context, TextRole::metricLabel, juce::Justification::centredLeft);
        g.setColour (COL_TEXT_TERTIARY);
        attack_lane_painter::drawFitting (g, { attack_band_summary::agreeText (summary, lane) }, head.withTrimmedRight (2),
                                          context, TextRole::unit, juce::Justification::centredRight);
        if (! stated)
        {
            g.setColour (COL_TEXT_SECONDARY);
            attack_lane_painter::drawFitting (g, { attack_band_summary::shownText (summary, lane, delayReason), "--" },
                                              cell, context, TextRole::readout, juce::Justification::centredLeft);
            continue;
        }
        const auto value = attack_band_summary::valueText (summary, lane, false);
        g.setColour (COL_OBSERVATORY_VALUE);
        attack_lane_painter::drawFitting (g, { value }, cell, context, TextRole::primaryValue,
                                          juce::Justification::centredLeft);
        const auto used = static_cast<int> (std::ceil (text_style::shownWidth (font (context, TextRole::primaryValue), value))) + 3;
        g.setColour (COL_TEXT_TERTIARY);
        attack_lane_painter::drawFitting (g, { attack_band_summary::unitFor (lane, attack_band_summary::delta (summary)) },
                                          cell.withTrimmedLeft (used).withTrimmedTop (cell.getHeight() / 3), context,
                                          TextRole::unit, juce::Justification::centredLeft);
    }
}

juce::String laneTooltip (std::size_t lane, bool delta)
{
    constexpr const char* differences[laneCount] {
        "DELAY: POST arrival minus PRE arrival of each recent hit in this band, as dots; the bar is the median. The figure under it says how many hits lie on the median's side of zero.",
        "ATT: the rise of each recent hit in this band, POST minus PRE, as dots; the bar is the median. Inside the shaded span (one period of the band) no difference can be told apart.",
        "REL: the fall from the band peak to peak - 20 dB of each recent hit, POST minus PRE, as dots; the bar is the median.",
        "LEVEL: the band's peak level of each recent hit, POST minus PRE, as dots; the bar is the median.",
    };
    constexpr const char* values[laneCount] {
        "DELAY needs PRE: POST arrival minus PRE arrival.",
        "ATT: POST's rise of each recent hit in this band, as dots; the bar is the median.",
        "REL: POST's fall from the band peak to peak - 20 dB of each recent hit, as dots; the bar is the median.",
        "LEVEL: POST's band peak level of each recent hit, as dots; the bar is the median.",
    };
    return (delta ? differences : values)[lane < laneCount ? lane : 0];
}

juce::String cardTooltip()
{
    return "The recent hits that rise in this band, summed up: each lane's median and how many hits agree. Click a dot to see that hit; END or LIVE returns here.";
}
}
