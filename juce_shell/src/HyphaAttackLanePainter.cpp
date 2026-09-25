#include "HyphaAttackLanePainter.h"

#include <cmath>
#include <initializer_list>

#include "HyphaAttackStage.h"
#include "HyphaAttackUiContract.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

namespace hypha::attack_lane_painter
{
using attack_lanes::index;
using attack_lanes::Lane;
using attack_lanes::Reason;
using typography::TextRole;

namespace
{
constexpr auto visualization = typography::Composition::visualization;

int lineHeight (const presentation::Context& context, TextRole role)
{
    return text_style::requiredLineHeight (typography::resolve (context, role, visualization));
}

juce::String shortReason (const juce::String& reason)
{
    return reason.upToFirstOccurrenceOf (" ", false, false); // NEXT, QUIET, PRE, POST, NO
}

juce::Rectangle<int> rectangleOf (attack_ui::Box box)
{
    return { box.x, box.y, box.width, box.height };
}

float unitHash (std::int64_t seed, int step) noexcept
{
    auto h = static_cast<std::uint64_t> (seed) * 0x9e3779b97f4a7c15ull
           + static_cast<std::uint64_t> (step + 1) * 0xbf58476d1ce4e5b9ull;
    h ^= h >> 31;
    h *= 0x94d049bb133111ebull;
    h ^= h >> 29;
    return static_cast<float> ((h >> 40) & 0xffffu) / 65535.0f * 2.0f - 1.0f;
}

juce::Rectangle<float> lanePlotInner (juce::Rectangle<int> plot)
{
    return plot.toFloat().reduced (static_cast<float> (attack_ui::laneInsetX),
                                   static_cast<float> (attack_ui::laneInsetY));
}

float baseFraction (attack_lanes::Scale scale) noexcept
{
    return scale.fromZero ? -scale.minimum / (scale.maximum - scale.minimum) : 0.0f;
}

// A short lane-colour mark identifies a readout; the number itself stays ivory.
void paintAccent (juce::Graphics& g, juce::Rectangle<int> row, juce::Colour colour, float alpha)
{
    const auto height = juce::jmin (static_cast<float> (row.getHeight()) - 6.0f, 18.0f);
    if (height < 4.0f)
        return;
    g.setColour (colour.withAlpha (alpha));
    g.fillRoundedRectangle ({ static_cast<float> (row.getX()),
                              static_cast<float> (row.getCentreY()) - height * 0.5f,
                              2.0f, height }, 1.0f);
}
}

bool drawFitting (juce::Graphics& g, std::initializer_list<juce::String> candidates,
                  juce::Rectangle<int> area, const presentation::Context& context,
                  TextRole role, juce::Justification justification, float tracking)
{
    const auto font = attack_stage::trackedFont (context, role, tracking);
    const auto style = typography::resolve (context, role, visualization);
    for (const auto& text : candidates)
        if (text.isNotEmpty()
            && text_style::requiredWidth (font, text, style) <= area.getWidth())
        {
            g.setFont (font);
            g.drawText (text, area, justification, false);
            return true;
        }
    return false;
}

juce::Colour colourFor (Lane lane) noexcept
{
    switch (lane)
    {
        case Lane::transient: return juce::Colour (attack_ui::transientColour);
        case Lane::strength:  return juce::Colour (attack_ui::strengthColour);
        case Lane::crest:     return juce::Colour (attack_ui::crestColour);
        case Lane::sharpness: return juce::Colour (attack_ui::sharpnessColour);
    }
    return COL_NORMAL;
}

juce::String nameFor (Lane lane)
{
    constexpr const char* names[] { "TRANSIENT", "STRENGTH", "CREST", "SHARPNESS" };
    return names[index (lane)];
}

juce::String codeFor (Lane lane)
{
    constexpr const char* codes[] { "TR", "ST", "CR", "SH" };
    return codes[index (lane)];
}

juce::String scaleCaption (Lane lane, bool delta)
{
    if (delta)
        return lane == Lane::sharpness ? "+/-1 acum" : "+/-12 dB";
    constexpr const char* absolute[] { "-12..24 dB", "-72..0 dBFS", "0..24 dB", "0..8 acum" };
    return absolute[index (lane)];
}

juce::String valueText (Lane lane, float value, bool delta, bool withUnit)
{
    if (! std::isfinite (value))
        return "--";
    const auto decimals = lane == Lane::sharpness ? 2 : 1;
    const auto step = decimals == 2 ? 100.0f : 10.0f;
    auto rounded = std::round (value * step) / step;
    if (rounded == 0.0f)
        rounded = 0.0f; // never print -0.0
    auto text = juce::String (rounded, decimals);
    if (delta && rounded >= 0.0f)
        text = "+" + text;
    if (! withUnit)
        return text;
    return text + (lane == Lane::sharpness ? " acum"
                 : lane == Lane::strength && ! delta ? " dBFS" : " dB");
}

juce::String reasonText (const attack_lanes::Hit& hit, Reason reason)
{
    switch (reason)
    {
        case Reason::value:        return {};
        case Reason::missing:      return "--";
        case Reason::nextHit:      return "NEXT HIT";
        case Reason::quietBody:    return "QUIET AFTER";
        case Reason::noMatch:
            return hit.pre.available && ! hit.post.available ? "PRE ONLY"
                 : hit.post.available && ! hit.pre.available ? "POST ONLY" : "NO PAIR";
    }
    return "--";
}

void paintLaneChrome (juce::Graphics& g, Lane lane, juce::Rectangle<int> label,
                      juce::Rectangle<int> plot, bool delta, const presentation::Context& context)
{
    const auto colour = colourFor (lane);
    if (! label.isEmpty())
    {
        auto cell = label.reduced (4, 1);
        const auto nameHeight = lineHeight (context, TextRole::metricLabel);
        const auto captionHeight = lineHeight (context, TextRole::legend);
        const auto caption = scaleCaption (lane, delta);
        const bool twoLines = caption.isNotEmpty()
                           && cell.getHeight() >= nameHeight + captionHeight;
        auto nameArea = twoLines ? cell.removeFromTop (cell.getHeight() / 2) : cell;
        g.setColour (colour);
        drawFitting (g, { nameFor (lane), codeFor (lane) }, nameArea, context,
                     TextRole::metricLabel, twoLines ? juce::Justification::bottomLeft
                                                     : juce::Justification::centredLeft,
                     attack_stage::labelTracking (context));
        if (twoLines)
        {
            g.setColour (COL_TEXT_TERTIARY);
            drawFitting (g, { caption }, cell, context, TextRole::legend,
                         juce::Justification::topLeft, attack_stage::captionTracking (context));
        }
    }

    attack_stage::paint (g, plot.toFloat(), 3.0f, 0.16f);
    const auto inner = lanePlotInner (plot);
    const auto fraction = baseFraction (attack_lanes::scaleFor (lane, delta));
    const auto baseY = inner.getBottom() - fraction * inner.getHeight();
    g.setColour (COL_TEXT_TERTIARY.withAlpha (delta ? 0.46f : 0.28f));
    g.drawHorizontalLine (juce::roundToInt (baseY), inner.getX(), inner.getRight());
    // Fixed-scale ticks at both plot edges: half range and full range on each side of zero.
    g.setColour (COL_TEXT_TERTIARY.withAlpha (0.30f));
    for (const auto tick : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        if (! delta && tick < 0.5f) continue;
        const auto y = juce::roundToInt (inner.getBottom() - tick * inner.getHeight());
        if (std::abs (static_cast<float> (y) - baseY) < 1.5f) continue;
        g.drawHorizontalLine (y, inner.getX(), inner.getX() + 4.0f);
        g.drawHorizontalLine (y, inner.getRight() - 4.0f, inner.getRight());
    }
}

void paintLaneValues (juce::Graphics& g, Lane lane, juce::Rectangle<int> plot,
                      juce::Rectangle<int> readout, const Frame& frame)
{
    const auto colour = colourFor (lane);
    const auto delta = frame.model.delta;
    const auto& context = frame.context;
    const auto inner = lanePlotInner (plot);
    const auto scale = attack_lanes::scaleFor (lane, delta);
    const auto yAt = [inner] (float fraction) { return inner.getBottom() - fraction * inner.getHeight(); };
    const auto baseY = yAt (baseFraction (scale));
    const auto window = static_cast<float> (attack_ui::windowSamples (frame.rate));
    const auto barWidth = static_cast<float> (juce::jlimit (2, 4, plot.getWidth() / 180));
    for (std::uint32_t item = 0; item < frame.model.count; ++item)
    {
        const auto& hit = frame.model.hits[item];
        const auto x = attack_ui::eventX (hit.sample, frame.latest, frame.rate, plot.getWidth());
        if (x < 0)
            continue;
        const auto centreX = static_cast<float> (plot.getX() + x) + 0.5f;
        const auto& cell = hit.cells[index (lane)];
        const bool selected = &hit == frame.selected;
        // Recency: the newest hits burn brightest, as they do on the time axis itself.
        const auto age = window > 0.0f
            ? juce::jlimit (0.0f, 1.0f, static_cast<float> (frame.latest - hit.sample) / window) : 0.0f;
        const auto life = 1.0f - 0.45f * age;
        if (cell.reason != Reason::value)
        {
            g.setColour (COL_TEXT_TERTIARY.withAlpha (selected ? 1.0f : 0.72f));
            g.drawEllipse (centreX - 1.6f, baseY - 1.6f, 3.2f, 3.2f, 0.8f);
            continue;
        }
        const auto extent = attack_lanes::extentFor (cell.value, scale);
        auto top = yAt (juce::jmax (extent.from, extent.to));
        auto bottom = yAt (juce::jmin (extent.from, extent.to));
        if (bottom - top < 1.0f)
        {
            top = baseY - 0.5f;
            bottom = baseY + 0.5f;
        }
        const juce::Rectangle<float> core (centreX - barWidth * 0.5f, top, barWidth, bottom - top);
        g.setColour (colour.withAlpha ((selected ? 0.34f : 0.15f) * life));
        g.fillRect (core.expanded (2.0f, 0.0f));
        g.setColour (colour.withAlpha ((selected ? 1.0f : 0.80f) * life));
        g.fillRect (core);
        // Tips, caps and spores keep the pure lane colour: brightening the gold lanes would
        // approach the pale selection colour and make the selected hit ambiguous.
        const bool rising = extent.to >= extent.from;
        const auto tipY = rising ? top : bottom - 1.0f;
        g.setColour (colour.withAlpha (life));
        g.fillRect (juce::Rectangle<float> (core.getX() - 0.5f, tipY, core.getWidth() + 1.0f, 1.0f));
        if (extent.clippedHigh || extent.clippedLow)
        {
            // An out-of-range value reaches the lane edge and keeps a cap; the exact
            // number stays in the readout.
            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (
                core.getX() - 1.5f, extent.clippedHigh ? inner.getY() : inner.getBottom() - 1.0f,
                core.getWidth() + 3.0f, 1.0f));
        }
        if (selected)
        {
            const auto sporeY = rising ? top : bottom;
            g.setColour (colour.withAlpha (0.24f));
            g.fillEllipse (centreX - 4.5f, sporeY - 4.5f, 9.0f, 9.0f);
            g.setColour (colour);
            g.fillEllipse (centreX - 2.2f, sporeY - 2.2f, 4.4f, 4.4f);
        }
    }

    if (readout.isEmpty())
        return;
    const auto cell = readout.reduced (6, 1);
    const auto* hit = frame.selected;
    paintAccent (g, readout, colour, hit != nullptr ? 0.9f : 0.35f);
    if (hit == nullptr)
    {
        g.setColour (COL_TEXT_TERTIARY);
        drawFitting (g, { "--" }, cell, context, TextRole::readout,
                     juce::Justification::centredLeft);
        return;
    }
    // The readout states the lane's own quantity only: POST - PRE when paired, the POST value
    // otherwise. Per-hit PRE and POST operands are not shown (SHARPNESS per hit is a difference
    // only until its aperture follows the onset).
    const auto& value = hit->cells[index (lane)];
    if (value.reason == Reason::value)
    {
        g.setColour (COL_OBSERVATORY_VALUE);
        drawFitting (g, { valueText (lane, value.value, delta, true),
                          valueText (lane, value.value, delta, false) },
                     cell, context, TextRole::secondaryValue, juce::Justification::centredLeft);
        return;
    }
    const auto reason = reasonText (*hit, value.reason);
    g.setColour (COL_TEXT_SECONDARY);
    drawFitting (g, { reason, shortReason (reason), "--" }, cell, context, TextRole::readout,
                 juce::Justification::centredLeft, attack_stage::captionTracking (context));
}

void paintLine (juce::Graphics& g, const attack_ui::Layout& layout, const Frame& frame)
{
    const auto* hit = frame.selected;
    for (const auto lane : attack_lanes::lanes)
    {
        auto cell = rectangleOf (attack_ui::lineCell (layout, index (lane)));
        const bool measured = hit != nullptr && hit->cells[index (lane)].reason == Reason::value;
        paintAccent (g, cell, colourFor (lane), measured ? 0.9f : 0.35f);
        cell.removeFromLeft (attack_ui::lineAccentWidth);
        const auto code = codeFor (lane);
        if (measured)
        {
            const auto value = hit->cells[index (lane)].value;
            g.setColour (COL_OBSERVATORY_VALUE);
            drawFitting (g, { code + " " + valueText (lane, value, frame.model.delta, true),
                              code + " " + valueText (lane, value, frame.model.delta, false),
                              valueText (lane, value, frame.model.delta, false) },
                         cell, frame.context, TextRole::readout,
                         juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (COL_TEXT_TERTIARY);
            drawFitting (g, { code + " --", "--" }, cell, frame.context,
                         TextRole::readout, juce::Justification::centredLeft);
        }
    }
}

void paintHistoryLabel (juce::Graphics& g, juce::Rectangle<int> area,
                        const presentation::Context& context)
{
    auto cell = area.reduced (4, 2);
    const auto nameHeight = lineHeight (context, TextRole::metricLabel);
    const auto captionHeight = lineHeight (context, TextRole::legend);
    if (cell.getHeight() < nameHeight)
        return;
    cell = cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (
        cell.getHeight(), nameHeight + captionHeight));
    g.setColour (COL_TEXT_SECONDARY);
    drawFitting (g, { "HISTORY", "HIST" }, cell.removeFromTop (nameHeight), context,
                 TextRole::metricLabel, juce::Justification::centredLeft,
                 attack_stage::labelTracking (context));
    g.setColour (COL_TEXT_TERTIARY);
    drawFitting (g, { "-72..0 dBFS", "dBFS" }, cell, context, TextRole::legend,
                 juce::Justification::centredLeft, attack_stage::captionTracking (context));
}

void paintSelectedTime (juce::Graphics& g, juce::Rectangle<int> area, const Frame& frame)
{
    auto cell = area.reduced (6, 2);
    const auto* hit = frame.selected;
    const auto titleHeight = lineHeight (frame.context, TextRole::legend);
    const auto valueHeight = lineHeight (frame.context, TextRole::secondaryValue);
    if (cell.getHeight() < titleHeight + valueHeight)
        return;
    cell = cell.withSizeKeepingCentre (cell.getWidth(), titleHeight + valueHeight);
    g.setColour (COL_TEXT_TERTIARY);
    drawFitting (g, { "SELECTED HIT", "HIT" }, cell.removeFromTop (titleHeight),
                 frame.context, TextRole::legend, juce::Justification::centredLeft,
                 attack_stage::captionTracking (frame.context));
    g.setColour (hit != nullptr ? COL_OBSERVATORY_VALUE : COL_TEXT_TERTIARY);
    auto text = juce::String ("--");
    if (hit != nullptr && frame.rate > 0)
    {
        auto seconds = std::round (static_cast<double> (hit->sample - frame.latest)
                                   / static_cast<double> (frame.rate) * 10.0) / 10.0;
        if (seconds == 0.0)
            seconds = 0.0;
        text = juce::String (seconds, 1) + " s";
    }
    drawFitting (g, { text }, cell, frame.context, TextRole::secondaryValue,
                 juce::Justification::centredLeft);
}

void paintHypha (juce::Graphics& g, float x, float top, float bottom, float bulbY,
                 std::int64_t seed)
{
    if (! std::isfinite (x) || ! (bottom - top >= 4.0f))
        return;
    const auto colour = juce::Colour (attack_ui::selectionColour);
    const auto steps = juce::jlimit (2, 40, static_cast<int> ((bottom - top) / 16.0f));
    // Control points stay inside x +/- 0.7 px, so the curve (inside their hull) stays there too.
    const auto drift = [seed, x] (int step) { return x + 0.7f * unitHash (seed, step); };
    juce::Path hypha;
    hypha.startNewSubPath (drift (0), top);
    for (int step = 1; step <= steps; ++step)
    {
        const auto y0 = top + (bottom - top) * static_cast<float> (step - 1) / static_cast<float> (steps);
        const auto y1 = top + (bottom - top) * static_cast<float> (step) / static_cast<float> (steps);
        hypha.cubicTo (drift (100 + step * 2), y0 + (y1 - y0) * 0.35f,
                       drift (101 + step * 2), y0 + (y1 - y0) * 0.70f,
                       drift (step), y1);
    }
    g.setColour (colour.withAlpha (0.14f));
    g.strokePath (hypha, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (colour.withAlpha (0.92f));
    g.strokePath (hypha, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    if (std::isfinite (bulbY))
    {
        g.setColour (colour.withAlpha (0.20f));
        g.fillEllipse (x - 5.0f, bulbY - 5.0f, 10.0f, 10.0f);
        g.setColour (colour.withAlpha (0.95f));
        g.fillEllipse (x - 2.3f, bulbY - 2.3f, 4.6f, 4.6f);
    }
    g.setColour (colour);
    g.fillEllipse (x - 1.2f, bottom - 1.2f, 2.4f, 2.4f);
}
}
