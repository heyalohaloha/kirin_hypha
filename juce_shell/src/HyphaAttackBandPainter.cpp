#include "HyphaAttackBandPainter.h"

#include <cmath>
#include <initializer_list>

#include "HyphaAttackDepth.h"
#include "HyphaAttackLanePainter.h"
#include "HyphaAttackStage.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

namespace hypha::attack_band_painter
{
namespace
{
using attack_lanes::Lane;
using typography::TextRole;
constexpr auto visualization = typography::Composition::visualization;
const auto postColour = juce::Colour (attack_ui::waveformColour);
const auto preColour = juce::Colour (attack_ui::preTraceColour);
const auto onsetColour = juce::Colour (attack_ui::selectionColour);

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

juce::String signedMs (float ms, int decimals)
{
    auto rounded = std::round (ms * std::pow (10.0f, static_cast<float> (decimals)))
                 / std::pow (10.0f, static_cast<float> (decimals));
    if (rounded == 0.0f)
        rounded = 0.0f;
    return (rounded >= 0.0f ? "+" : "") + juce::String (rounded, decimals) + " ms";
}

// A pane's parts: the caption line, one plot (or PRE above POST) and the time labels.
struct Parts
{
    juce::Rectangle<int> caption, labels;
    std::array<juce::Rectangle<float>, 2> plots {};
    std::size_t rows = 1;
};

Parts partsOf (juce::Rectangle<int> pane, const presentation::Context& context, bool twoRows)
{
    Parts parts;
    auto inner = pane.reduced (6, 3);
    parts.caption = inner.removeFromTop (lineHeight (context, TextRole::legend));
    parts.labels = inner.removeFromBottom (lineHeight (context, TextRole::axis));
    auto plot = inner.toFloat().reduced (2.0f, 1.0f);
    if (twoRows && plot.getHeight() >= 24.0f)
    {
        parts.rows = 2;
        parts.plots[0] = plot.removeFromTop ((plot.getHeight() - 4.0f) * 0.5f);
        plot.removeFromTop (4.0f);
    }
    parts.plots[parts.rows - 1] = plot;
    return parts;
}

struct Axis
{
    juce::Rectangle<float> plot;
    float fromMs = 0.0f;
    float toMs = 1.0f;

    float x (float ms) const noexcept
    {
        return plot.getX() + plot.getWidth() * (ms - fromMs) / (toMs - fromMs);
    }

    float y (float db) const noexcept
    {
        const auto level = std::isfinite (db)
            ? juce::jlimit (0.0f, 1.0f, (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb)
            : 0.0f;
        return plot.getBottom() - level * plot.getHeight();
    }
};

Axis axisFor (juce::Rectangle<float> plot, bool head) noexcept
{
    return head ? Axis { plot, attack_band::headFromMs, attack_band::headToMs }
                : Axis { plot, attack_band::tailFromMs, attack_band::tailToMs };
}

// The envelope points inside the pane's window: the edge, or the body closed to the floor.
template <std::size_t N>
juce::Path envelopePath (const float (&points)[N], float pointsFromMs, float pointsToMs,
                         const Axis& axis, bool closed)
{
    juce::Path path;
    const auto step = (pointsToMs - pointsFromMs) / static_cast<float> (N);
    bool started = false;
    float lastX = 0.0f;
    for (std::size_t point = 0; point < N; ++point)
    {
        const auto ms = pointsFromMs + (static_cast<float> (point) + 0.5f) * step;
        if (ms < axis.fromMs || ms > axis.toMs)
            continue;
        const juce::Point<float> at { axis.x (ms), axis.y (points[point]) };
        if (! started)
        {
            if (closed)
            {
                path.startNewSubPath (at.x, axis.plot.getBottom());
                path.lineTo (at);
            }
            else
                path.startNewSubPath (at);
            started = true;
        }
        else
            path.lineTo (at);
        lastX = at.x;
    }
    if (started && closed)
    {
        path.lineTo (lastX, axis.plot.getBottom());
        path.closeSubPath();
    }
    return path;
}

void paintSide (juce::Graphics& g, const KirinAttackBandSide& side, bool head, const Axis& axis,
                bool post)
{
    if (side.available == 0)
        return;
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (axis.plot.getSmallestIntegerContainer());
    const auto edge = head
        ? envelopePath (side.head_dbfs, attack_band::headPointsFromMs, attack_band::headPointsToMs, axis, false)
        : envelopePath (side.tail_dbfs, attack_band::tailFromMs, attack_band::tailToMs, axis, false);
    if (! post)
    {
        // The PRE reference stays a plain light line over the POST body, as in HISTORY.
        g.setColour (preColour.withAlpha (0.85f));
        g.strokePath (edge, juce::PathStrokeType (0.9f));
        return;
    }
    const auto body = head
        ? envelopePath (side.head_dbfs, attack_band::headPointsFromMs, attack_band::headPointsToMs, axis, true)
        : envelopePath (side.tail_dbfs, attack_band::tailFromMs, attack_band::tailToMs, axis, true);
    const auto& depth = attack_depth::look();
    const auto& plot = axis.plot;
    if (depth.lift > 0.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (juce::jmin (1.0f, 0.85f * depth.lift)));
        g.fillPath (body, juce::AffineTransform::translation (1.0f, 1.5f));
    }
    g.setGradientFill (juce::ColourGradient (postColour.withAlpha (0.34f), plot.getX(), plot.getY(),
                                             postColour.withAlpha (0.04f), plot.getX(), plot.getBottom(),
                                             false));
    g.fillPath (body);
    attack_depth::lightVolume (g, edge, edge, postColour, depth.fresnel + 0.03f * depth.bloom,
                               depth.specular, 1.4f);
    g.setColour (postColour.withAlpha (0.20f));
    g.strokePath (edge, juce::PathStrokeType (2.6f));
    g.setColour (postColour.withAlpha (0.92f));
    g.strokePath (edge, juce::PathStrokeType (1.0f));
}

// A measured time between two marks: a bracket with its value beside it, ivory like every value.
void paintBracket (juce::Graphics& g, const Axis& axis, float fromMs, float toMs, float y,
                   const juce::String& text, const presentation::Context& context)
{
    const auto x0 = axis.x (fromMs);
    const auto x1 = axis.x (toMs);
    g.setColour (COL_OBSERVATORY_VALUE.withAlpha (0.9f));
    g.drawLine (x0, y, x1, y, 1.2f);
    g.drawLine (x0, y - 4.0f, x0, y + 4.0f, 1.0f);
    g.drawLine (x1, y - 4.0f, x1, y + 4.0f, 1.0f);
    g.setFont (monoFont (context, TextRole::readout, visualization));
    const auto right = juce::jmax (x0, x1) + 5.0f;
    const auto width = 84.0f;
    const bool fitsRight = right + width <= axis.plot.getRight();
    g.setColour (COL_OBSERVATORY_VALUE);
    text_style::drawText (g, text,
                          fitsRight ? juce::Rectangle<float> (right, y - 9.0f, width, 18.0f)
                                    : juce::Rectangle<float> (juce::jmin (x0, x1) - 5.0f - width, y - 9.0f,
                                                              width, 18.0f),
                          fitsRight ? juce::Justification::centredLeft : juce::Justification::centredRight,
                          false);
}

void paintHeadMarks (juce::Graphics& g, const KirinAttackBandHit& hit, const Axis& axis,
                     bool pre, bool post, const presentation::Context& context)
{
    const auto& plot = axis.plot;
    const auto arrival = [&] (const KirinAttackBandSide& side, juce::Colour colour) {
        if (side.available == 0 || side.arrival_available == 0)
            return;
        g.setColour (colour.withAlpha (0.70f));
        g.drawVerticalLine (juce::roundToInt (axis.x (side.arrival_ms)), plot.getY(), plot.getBottom()); };
    if (pre) arrival (hit.pre, preColour);
    if (post) arrival (hit.post, postColour);
    if (pre && post && hit.delay_available != 0 && hit.pre.arrival_available != 0
        && hit.post.arrival_available != 0)
        paintBracket (g, axis, hit.pre.arrival_ms, hit.post.arrival_ms, plot.getY() + 9.0f,
                      signedMs (hit.delay_ms, 1), context);
}

void paintTailMarks (juce::Graphics& g, const KirinAttackBandHit& hit, const Axis& axis,
                     bool pre, bool post, const presentation::Context& context)
{
    const auto end = [] (const KirinAttackBandSide& side) { return side.peak_ms + side.release_ms; };
    const auto mark = [&] (const KirinAttackBandSide& side, juce::Colour colour) {
        if (side.available == 0 || side.release_available == 0)
            return;
        const auto x = axis.x (end (side));
        const auto y = axis.y (side.level_dbfs - 20.0f);
        g.setColour (colour.withAlpha (0.85f));
        g.drawLine (x, y - 6.0f, x, y + 6.0f, 1.2f); };
    if (pre) mark (hit.pre, preColour);
    if (post) mark (hit.post, postColour);
    if (pre && post && hit.pre.available != 0 && hit.post.available != 0
        && hit.pre.release_available != 0 && hit.post.release_available != 0)
    {
        const auto y = juce::jmin (axis.y (hit.pre.level_dbfs - 20.0f),
                                   axis.y (hit.post.level_dbfs - 20.0f)) - 12.0f;
        paintBracket (g, axis, end (hit.pre), end (hit.post),
                      juce::jmax (axis.plot.getY() + 9.0f, y),
                      signedMs (hit.post.release_ms - hit.pre.release_ms, 0), context);
    }
}

void paintPaneValues (juce::Graphics& g, juce::Rectangle<int> pane, bool head,
                      const KirinAttackBandHit& hit, const presentation::Context& context,
                      bool twoRows)
{
    const auto parts = partsOf (pane, context, twoRows);
    for (std::size_t row = 0; row < parts.rows; ++row)
    {
        const auto axis = axisFor (parts.plots[row], head);
        const bool pre = parts.rows == 1 || row == 0;
        const bool post = parts.rows == 1 || row == 1;
        if (post) paintSide (g, hit.post, head, axis, true);
        if (pre) paintSide (g, hit.pre, head, axis, false);
        if (head) paintHeadMarks (g, hit, axis, pre, post, context);
        else paintTailMarks (g, hit, axis, pre, post, context);
        if (parts.rows == 2)
        {
            g.setColour (COL_TEXT_TERTIARY);
            g.setFont (monoFont (context, TextRole::legend, visualization)
                           .withExtraKerningFactor (attack_stage::captionTracking (context)));
            text_style::drawText (g, row == 0 ? "PRE" : "POST",
                                  parts.plots[row].getSmallestIntegerContainer().reduced (4, 1),
                                  juce::Justification::topLeft, false);
        }
    }
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

juce::String chipTooltip (std::uint8_t band)
{
    const auto* entry = attack_band::bandFor (band);
    if (entry == nullptr)
        return "ALL: every hit measured on the whole signal, as DRUM always has.";
    return "Octave band " + nameText (band) + ": " + rangeText (band)
         + ". Each hit is filtered to this band on PRE and POST and measured once its ring-out is complete. Time resolution: one period, "
         + attack_lane_painter::resolutionText (entry->periodMs()) + ".";
}

juce::String pendingTooltip()
{
    return "PRE has not sent this band yet. A PRE older than bands never does: update PRE to compare the band.";
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
            return "DELAY: POST arrival minus PRE arrival in this band, ms. Arrival is where the envelope rises through its peak - 20 dB.";
        case Lane::attackTime:
            return "ATT: the rise from 10 % to 90 % of the band peak, ms. Below the band's time resolution it reads as an upper bound.";
        case Lane::release:
            return "REL: the fall from the band peak to peak - 20 dB, ms. Cut off by the next hit, it stays empty.";
        case Lane::level:
            return "LEVEL: the band's peak envelope level. POST - PRE in dB when paired, dBFS otherwise.";
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

void paintPaneChrome (juce::Graphics& g, const attack_ui::Layout& layout,
                      const presentation::Context& context, bool twoRows, bool paired,
                      bool prePending)
{
    const auto& depth = attack_depth::look();
    for (const bool head : { true, false })
    {
        const auto pane = rectangleOf (head ? attack_band::headPane (layout) : attack_band::tailPane (layout));
        if (pane.isEmpty())
            continue;
        attack_stage::paint (g, pane.toFloat(), 4.0f, 0.30f, true);
        const auto parts = partsOf (pane, context, twoRows);
        g.setFont (monoFont (context, TextRole::legend, visualization)
                       .withExtraKerningFactor (attack_stage::captionTracking (context)));
        g.setColour (COL_TEXT_SECONDARY);
        text_style::drawText (g, head ? "HEAD" : "TAIL", parts.caption, juce::Justification::centredLeft, false);
        g.setColour (COL_TEXT_TERTIARY);
        attack_lane_painter::drawFitting (
            g, { head ? (prePending ? "PRE: NO BAND YET" : paired ? "PRE / POST" : "POST") : "PEAK -20 dB" },
            parts.caption, context, TextRole::legend, juce::Justification::centredRight);
        g.setFont (monoFont (context, TextRole::axis, visualization));
        text_style::drawText (g, head ? "-5" : "0", parts.labels, juce::Justification::centredLeft, false);
        text_style::drawText (g, head ? "+40 ms" : "+300 ms", parts.labels, juce::Justification::centredRight, false);
        for (std::size_t row = 0; row < parts.rows; ++row)
        {
            const auto axis = axisFor (parts.plots[row], head);
            const auto& plot = axis.plot;
            g.setColour (COL_TEXT_TERTIARY.withAlpha (0.14f));
            for (const auto db : { -24.0f, -48.0f })
                g.drawHorizontalLine (juce::roundToInt (axis.y (db)), plot.getX(), plot.getRight());
            g.setColour (postColour.withAlpha (0.14f));
            g.drawHorizontalLine (juce::roundToInt (plot.getBottom() - 0.5f), plot.getX(), plot.getRight());
            attack_depth::engraveLip (g, plot.getBottom() - 0.5f, plot.getX(), plot.getRight());
            if (depth.graticule > 0.0f)
            {
                g.setColour (COL_TEXT_TERTIARY.withAlpha (juce::jmin (1.0f, depth.graticule * 2.0f)));
                for (int db = -12; db > static_cast<int> (attack_ui::absoluteFloorDb); db -= 12)
                {
                    const auto y = juce::roundToInt (axis.y (static_cast<float> (db)));
                    g.drawHorizontalLine (y, plot.getX(), plot.getX() + 3.0f);
                    g.drawHorizontalLine (y, plot.getRight() - 3.0f, plot.getRight());
                }
                // Time ticks on the walls: HEAD every 5 ms, TAIL every 50 ms, longer at the tens.
                const auto tick = head ? 5.0f : 50.0f;
                for (auto ms = axis.fromMs; ms <= axis.toMs; ms += tick)
                {
                    const auto x = juce::roundToInt (axis.x (ms));
                    const auto length = std::fmod (ms, tick * 2.0f) == 0.0f ? 4.0f : 2.0f;
                    g.drawVerticalLine (x, plot.getY(), plot.getY() + length);
                    g.drawVerticalLine (x, plot.getBottom() - length, plot.getBottom());
                }
            }
            if (head)
            {
                // The onset is the zero of HEAD's time axis.
                g.setColour (onsetColour.withAlpha (0.45f));
                g.drawVerticalLine (juce::roundToInt (axis.x (0.0f)), plot.getY(), plot.getBottom());
            }
        }
    }
}

void paintPanes (juce::Graphics& g, const attack_ui::Layout& layout,
                 const presentation::Context& context, const KirinAttackBandHit* hit, bool twoRows)
{
    for (const bool head : { true, false })
    {
        const auto pane = rectangleOf (head ? attack_band::headPane (layout) : attack_band::tailPane (layout));
        if (pane.isEmpty())
            continue;
        if (hit == nullptr)
        {
            const auto parts = partsOf (pane, context, false);
            g.setColour (COL_TEXT_TERTIARY);
            g.setFont (monoFont (context, TextRole::status, visualization));
            text_style::drawText (g, "NO HIT", parts.plots[0].getSmallestIntegerContainer(),
                                  juce::Justification::centred, false);
            continue;
        }
        paintPaneValues (g, pane, head, *hit, context, twoRows);
    }
}

void paintGlanceCaption (juce::Graphics& g, juce::Rectangle<int> history,
                         const presentation::Context& context, std::uint8_t band)
{
    if (band == 0 || history.isEmpty())
        return;
    g.setColour (COL_TEXT_SECONDARY);
    g.setFont (monoFont (context, TextRole::legend, visualization));
    text_style::drawText (g, nameText (band), history.reduced (6, 3), juce::Justification::topLeft, false);
}
}
