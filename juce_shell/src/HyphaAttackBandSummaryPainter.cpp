#include "HyphaAttackBandSummaryPainter.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>

#include "HyphaAttackBandSummaryStyle.h"

// The summary's number lines: their chrome, each hit's dot, the spread and the median, and the
// readouts beside them. The words are in HyphaAttackBandSummaryWords.cpp.
namespace hypha::attack_band_summary_painter
{
namespace
{
using attack_band_summary::laneCount;
using attack_band_summary::laneOf;
using namespace style;

// The part of a lane's plot the scale spans, clear of the well's walls.
juce::Rectangle<float> lineArea (juce::Rectangle<int> plot)
{
    return plot.toFloat().reduced (12.0f, 2.0f);
}

float xOf (const attack_band_summary::Scale& scale, juce::Rectangle<float> area, float value)
{
    const auto t = juce::jlimit (0.0f, 1.0f, (value - scale.from) / (scale.to - scale.from));
    return area.getX() + t * area.getWidth();
}

bool tall (juce::Rectangle<int> plot) noexcept
{
    return plot.getHeight() >= 30;
}
}

void paintLaneChrome (juce::Graphics& g, std::size_t lane, juce::Rectangle<int> plot, bool delta,
                      const presentation::Context& context)
{
    if (plot.isEmpty())
        return;
    attack_stage::paint (g, plot.toFloat(), 3.0f, 0.18f, false);
    const auto scale = attack_band_summary::scaleFor (lane, delta);
    if (scale.none)
        return;
    const auto area = lineArea (plot);
    g.setColour (COL_TEXT_TERTIARY.withAlpha (0.35f));
    for (const auto t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        g.drawVerticalLine (juce::roundToInt (area.getX() + t * area.getWidth()), area.getBottom() - 4.0f,
                            area.getBottom());
    if (delta)
    {
        // Zero: no change.
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.55f));
        g.drawVerticalLine (juce::roundToInt (xOf (scale, area, 0.0f)), area.getY(), area.getBottom());
    }
    if (tall (plot))
    {
        // Every scale ends on a whole number.
        const auto ends = plot.reduced (4, 1);
        const auto left = attack_band_summary::number (scale.from, 0, delta);
        const auto right = attack_band_summary::number (scale.to, 0, delta);
        text (g, left, ends, context, TextRole::axis, COL_TEXT_TERTIARY.withAlpha (0.7f), juce::Justification::bottomLeft);
        text (g, right, ends, context, TextRole::axis, COL_TEXT_TERTIARY.withAlpha (0.7f), juce::Justification::bottomRight);
    }
}

void paintAxisRow (juce::Graphics& g, juce::Rectangle<int> row, juce::Rectangle<int> linePlot, bool delta,
                   const presentation::Context& context)
{
    if (row.isEmpty() || linePlot.isEmpty())
        return;
    const auto area = lineArea (linePlot);
    const auto inner = row.withX (juce::roundToInt (area.getX())).withWidth (juce::roundToInt (area.getWidth()));
    if (! delta)
    {
        text (g, "POST VALUES", inner, context, TextRole::legend, COL_TEXT_SECONDARY, juce::Justification::centred);
        return;
    }
    // Both ends in the same words, shortened together so the row stays symmetric about zero.
    const auto centre = juce::jmin (120, inner.getWidth() / 3);
    const auto side = (inner.getWidth() - centre) / 2;
    constexpr std::pair<const char*, const char*> ends[] { { "- SMALLER / EARLIER", "LATER / LARGER +" },
                                                           { "- SMALLER", "LARGER +" }, { "-", "+" } };
    for (const auto& [less, more] : ends)
        if (fits (context, TextRole::legend, less, side) && fits (context, TextRole::legend, more, side))
        {
            text (g, less, inner.withWidth (side), context, TextRole::legend, COL_TEXT_TERTIARY,
                  juce::Justification::centredLeft);
            text (g, more, inner.withTrimmedLeft (inner.getWidth() - side), context, TextRole::legend,
                  COL_TEXT_TERTIARY, juce::Justification::centredRight);
            break;
        }
    g.setColour (COL_TEXT_SECONDARY);
    attack_lane_painter::drawFitting (g, { "0 = SAME", "0" }, inner.withSizeKeepingCentre (centre, inner.getHeight()),
                                      context, TextRole::legend, juce::Justification::centred);
}

juce::Point<float> dotCentre (const KirinAttackBandSummary& summary, std::size_t lane, juce::Rectangle<int> plot,
                              std::size_t index) noexcept
{
    const auto scale = attack_band_summary::scaleFor (lane, attack_band_summary::delta (summary));
    const auto area = lineArea (plot);
    const auto value = laneOf (summary, lane).values[index < KIRIN_ATTACK_BAND_SUMMARY_HITS ? index : 0];
    // Three rows in a tall lane, so hits with the same value stay countable.
    const auto dy = tall (plot) ? static_cast<float> (static_cast<int> (index % 3) - 1) * 4.0f : 0.0f;
    return { xOf (scale, area, value), area.getCentreY() + dy };
}

int dotAt (const KirinAttackBandSummary& summary, std::size_t lane, juce::Rectangle<int> plot,
           juce::Point<int> point) noexcept
{
    int best = -1;
    auto bestDistance = 8.0f;
    for (std::size_t index = 0; index < summary.count && index < KIRIN_ATTACK_BAND_SUMMARY_HITS; ++index)
    {
        if (! std::isfinite (laneOf (summary, lane).values[index]))
            continue;
        const auto distance = dotCentre (summary, lane, plot, index).getDistanceFrom (point.toFloat());
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int> (index);
        }
    }
    return best;
}

void paintLaneValues (juce::Graphics& g, std::size_t lane, juce::Rectangle<int> plot,
                      const KirinAttackBandSummary& summary, int ringed, const presentation::Context&)
{
    const auto& entry = laneOf (summary, lane);
    const auto scale = attack_band_summary::scaleFor (lane, attack_band_summary::delta (summary));
    if (plot.isEmpty() || scale.none || entry.count == 0)
        return;
    const auto colour = colourOf (lane);
    const auto area = lineArea (plot);
    if (attack_band_summary::delta (summary) && entry.within > 0.0f)
    {
        // Inside the band's resolution nothing can be told apart.
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (scale, area, -entry.within), area.getY(),
                                                                xOf (scale, area, entry.within), area.getBottom()));
    }
    g.setColour (colour.withAlpha (0.40f));
    g.drawLine (xOf (scale, area, entry.low), area.getCentreY(), xOf (scale, area, entry.high), area.getCentreY(),
                tall (plot) ? 2.0f : 1.5f);
    const auto radius = tall (plot) ? 3.3f : 2.4f;
    for (std::size_t index = 0; index < summary.count && index < KIRIN_ATTACK_BAND_SUMMARY_HITS; ++index)
    {
        if (! std::isfinite (entry.values[index]))
            continue;
        const auto centre = dotCentre (summary, lane, plot, index);
        const bool ring = static_cast<int> (index) == ringed;
        g.setColour (colour.withAlpha (ring ? 1.0f : 0.55f));
        g.fillEllipse (juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (centre));
        if (ring)
        {
            g.setColour (COL_OBSERVATORY_VALUE.withAlpha (0.85f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * radius + 4.0f, 2.0f * radius + 4.0f).withCentre (centre), 1.0f);
        }
    }
    if (entry.state == KIRIN_ATTACK_BAND_LANE_VALUE)
    {
        const auto x = xOf (scale, area, entry.median);
        g.setColour (colour.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (5.0f, area.getHeight() * 0.8f).withCentre ({ x, area.getCentreY() }));
        g.setColour (colour);
        g.fillRect (juce::Rectangle<float> (2.0f, area.getHeight() * 0.8f).withCentre ({ x, area.getCentreY() }));
    }
}

void paintReadout (juce::Graphics& g, std::size_t lane, juce::Rectangle<int> cell,
                   const KirinAttackBandSummary& summary, const juce::String& delayReason,
                   const presentation::Context& context)
{
    if (cell.isEmpty())
        return;
    const auto& entry = laneOf (summary, lane);
    const bool stated = entry.state == KIRIN_ATTACK_BAND_LANE_VALUE;
    attack_lane_painter::paintAccent (g, cell, colourOf (lane), stated ? 0.9f : 0.35f);
    auto inner = cell.reduced (8, 1);
    const auto value = attack_band_summary::shownText (summary, lane, delayReason);
    const auto brief = attack_band_summary::shownText (summary, lane, delayReason, false);
    const auto word = attack_band_summary::wordText (summary, lane);
    const auto agree = attack_band_summary::agreeText (summary, lane);
    const auto valueColour = stated ? COL_OBSERVATORY_VALUE : COL_TEXT_SECONDARY;
    const auto wordColour = stated ? colourOf (lane) : COL_TEXT_TERTIARY;
    const auto wordLine = lineHeight (context, TextRole::legend);
    for (const auto role : { TextRole::primaryValue, TextRole::secondaryValue })
    {
        const auto valueLine = lineHeight (context, role);
        if (word.isEmpty() || inner.getHeight() < valueLine + wordLine)
            continue;
        auto block = inner.withSizeKeepingCentre (inner.getWidth(), valueLine + wordLine);
        g.setColour (valueColour);
        attack_lane_painter::drawFitting (g, { value, brief }, block.removeFromTop (valueLine), context, role,
                                          juce::Justification::centredLeft);
        g.setColour (wordColour);
        attack_lane_painter::drawFitting (g, { agree.isEmpty() ? word : word + "  " + agree, word }, block,
                                          context, TextRole::legend, juce::Justification::centredLeft);
        return;
    }
    // One line: the value, then the agreement where it fits.
    const auto role = inner.getHeight() >= lineHeight (context, TextRole::secondaryValue) ? TextRole::secondaryValue
                                                                                         : TextRole::readout;
    g.setColour (valueColour);
    attack_lane_painter::drawFitting (g, { value, brief, "--" }, inner, context, role,
                                      juce::Justification::centredLeft);
    const auto used = static_cast<int> (std::ceil (text_style::shownWidth (font (context, role), value))) + 8;
    g.setColour (wordColour);
    attack_lane_painter::drawFitting (g, { agree, "" }, inner.withTrimmedLeft (used), context, TextRole::legend,
                                      juce::Justification::centredLeft);
}

void paintHitReadout (juce::Graphics& g, attack_lanes::Lane lane, juce::Rectangle<int> cell,
                      const attack_lanes::Hit* hit, bool delta, const presentation::Context& context)
{
    if (cell.isEmpty())
        return;
    const auto measured = hit != nullptr && attack_lane_painter::stated (hit->cells[attack_lanes::index (lane)]);
    attack_lane_painter::paintAccent (g, cell, attack_lane_painter::colourFor (lane), measured ? 0.9f : 0.35f);
    const auto inner = cell.reduced (8, 1);
    if (hit == nullptr)
    {
        g.setColour (COL_TEXT_TERTIARY);
        attack_lane_painter::drawFitting (g, { "--" }, inner, context, TextRole::readout,
                                          juce::Justification::centredLeft);
        return;
    }
    const auto reason = hit->cells[attack_lanes::index (lane)].reason;
    if (measured)
    {
        g.setColour (COL_OBSERVATORY_VALUE);
        attack_lane_painter::drawFitting (g, { attack_lane_painter::cellText (*hit, lane, delta, true),
                                               attack_lane_painter::cellText (*hit, lane, delta, false) },
                                          inner, context, TextRole::secondaryValue, juce::Justification::centredLeft);
        return;
    }
    g.setColour (COL_TEXT_SECONDARY);
    attack_lane_painter::drawFitting (g, { attack_lane_painter::reasonText (*hit, reason),
                                           attack_lane_painter::shortReasonText (*hit, reason), "--" },
                                      inner, context, TextRole::readout, juce::Justification::centredLeft);
}

std::array<juce::Rectangle<int>, attack_band_summary::laneCount> rowPlots (juce::Rectangle<int> area,
                                                                          const presentation::Context& context)
{
    std::array<juce::Rectangle<int>, laneCount> plots {};
    auto inner = area.reduced (8, 3);
    inner.removeFromTop (lineHeight (context, TextRole::legend) + 2);
    const auto legend = font (context, TextRole::legend);
    const auto codeWidth = juce::roundToInt (std::ceil (text_style::shownWidth (legend, "DL"))) + 10;
    const auto wordWidth = juce::roundToInt (std::ceil (text_style::shownWidth (legend, "UPDATE PRE"))) + 10;
    const auto rowHeight = inner.getHeight() / static_cast<int> (laneCount);
    if (rowHeight < 8 || inner.getWidth() < codeWidth + wordWidth + 60)
        return plots;
    for (auto& plot : plots)
    {
        auto row = inner.removeFromTop (rowHeight);
        row.removeFromLeft (codeWidth);
        row.removeFromRight (wordWidth);
        plot = row.reduced (0, 1);
    }
    return plots;
}
}
