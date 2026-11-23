#include "HyphaLuFacePainter.h"

#include "HyphaMeterGlass.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"
#include "HyphaVuNeedlePainter.h"

#include <array>
#include <cmath>

namespace hypha::hybrid_vu::lu_face
{
namespace
{
using needle_painter::dialGeometry;
using needle_painter::dialPoint;
using needle_painter::DialGeometry;

const auto ivory = COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.34f);
// The set's band stays behind the needles and figures: a solid, darker tone of the Reference cyan.
const auto bandColour = COL_SPECTRUM_DELTA.interpolatedWith (BG, 0.52f);

float scaled (float value, float width, float minimum = 0.7f) noexcept
{
    return juce::jmax (minimum, value * width / 900.0f);
}

bool compactFace (juce::Rectangle<int> bounds) noexcept { return bounds.getWidth() * 0.964f < 410.0f; }

constexpr auto instrument = typography::Composition::instrument;
using typography::TextRole;

// The face reads at the 12 px floor the Reference pages use, at every editor size.
presentation::Context faceContext (presentation::Context context) noexcept
{
    context.referenceCompact = true;
    return context;
}

void note (std::vector<TextBox>* trace, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font)
{
    if (trace != nullptr) trace->push_back ({ text, area, font.getHeight() });
}

void drawFigures (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                  const presentation::Context& context, TextRole role, juce::Colour colour,
                  juce::Justification justification, std::vector<TextBox>* trace)
{
    const auto font = monoFont (context, role, instrument);
    g.setColour (colour);
    drawTabularText (g, font, text, area, justification);
    note (trace, text, area, font);
}

// A name or a label that stays as written (a song title is never translated).
void drawFixed (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                const presentation::Context& context, TextRole role, juce::Colour colour,
                juce::Justification justification, std::vector<TextBox>* trace, float tracking = 0.0f)
{
    g.setColour (colour);
    g.setFont (labelFont (context, role, instrument).withExtraKerningFactor (tracking));
    text_style::drawText (g, text, area, justification, true, text_style::LabelPolicy::fixed);
    note (trace, text, area, labelFont (context, role, instrument));
}

juce::Point<float> along (const DialGeometry& dial, float normalized, float reach)
{
    return dial.pivot + (dialPoint (dial, normalized) - dial.pivot) * reach;
}

void paintBand (juce::Graphics& g, const DialGeometry& dial, float width, const lu_needle::Facts& facts)
{
    const auto zero = facts.playedZero();
    if (! facts.band.valid() || ! std::isfinite (zero)) return;
    const auto low = facts.band.lowLufs - zero, high = facts.band.highLufs - zero;
    if (high < lu_needle::scaleLowLu || low > lu_needle::scaleHighLu) return; // the whole range is off the scale
    const auto from = lu_needle::normalized (low), to = lu_needle::normalized (high);
    juce::Path band;
    constexpr int steps = 24;
    for (int step = 0; step <= steps; ++step)
    {
        const auto point = along (dial, juce::jmap (static_cast<float> (step) / steps, from, to), 0.925f);
        if (step == 0) band.startNewSubPath (point); else band.lineTo (point);
    }
    g.setColour (bandColour);
    g.strokePath (band, juce::PathStrokeType (scaled (9.0f, width, 3.5f), juce::PathStrokeType::curved,
                                              juce::PathStrokeType::butt));
}

void paintNeedle (juce::Graphics& g, const DialGeometry& dial, float width, Needle needle, float normalized)
{
    if (needle == Needle::own)
    {
        const auto end = along (dial, normalized, 1.11f);
        g.setColour (COL_FLORA_BR.withAlpha (0.20f));
        g.drawLine ({ dial.pivot, end }, scaled (5.2f, width, 1.5f));
        g.setColour (ivory.brighter (0.09f));
        g.drawLine ({ dial.pivot, end }, scaled (2.7f, width, 0.95f));
        return;
    }
    // The two companions are thinner and stop at the arc, so the main needle is never mistaken.
    g.setColour (needleColour (needle));
    g.drawLine ({ dial.pivot, along (dial, normalized, 1.0f) }, scaled (1.8f, width, 1.0f));
}

struct LegendItem { const char* name; Needle needle; };

float legendItemWidth (const juce::Font& font, const LegendItem& item)
{ return font.getHeight() * 1.45f + std::ceil (font.getStringWidthFloat (item.name)) + 2.0f; }

float legendWidth (const juce::Font& font, const std::vector<LegendItem>& items)
{
    auto width = 0.0f;
    for (const auto& item : items) width += legendItemWidth (font, item) + (width > 0.0f ? font.getHeight() * 0.9f : 0.0f);
    return width;
}

void paintLegend (juce::Graphics& g, juce::Rectangle<float> row, const std::vector<LegendItem>& items,
                  const presentation::Context& context, std::vector<TextBox>* trace)
{
    const auto font = labelFont (context, TextRole::legend, instrument);
    const auto swatch = font.getHeight() * 1.1f, gap = font.getHeight() * 0.35f;
    auto x = row.getX();
    for (const auto& item : items)
    {
        const auto textWidth = std::ceil (font.getStringWidthFloat (item.name)) + 2.0f;
        if (x + swatch + gap + textWidth > row.getRight() + 0.5f) break;
        g.setColour (needleColour (item.needle));
        g.fillRect (juce::Rectangle<float> (x, row.getCentreY() - 1.0f, swatch, item.needle == Needle::own ? 3.0f : 2.0f));
        drawFixed (g, item.name, { x + swatch + gap, row.getY(), textWidth, row.getHeight() }, context,
                   TextRole::legend, COL_TEXT_SECONDARY, juce::Justification::centredLeft, trace);
        x += swatch + gap + textWidth + font.getHeight() * 0.9f;
    }
}

// The Reference zero stands on the glass, above the gold rim and inside its curved corners: one row below
// both hubs, and at the small sizes two rows between them.
void paintZeroText (juce::Graphics& g, juce::Rectangle<int> bounds, const Frame& view, bool compact,
                    std::vector<TextBox>* trace)
{
    namespace glass = metric_face::glass;
    const auto context = faceContext (view.vu.presentation);
    const auto font = labelFont (context, TextRole::legend, instrument);
    const auto line = juce::jmax (12, static_cast<int> (std::ceil (font.getHeight())));
    const auto floorPixel = static_cast<int> (std::floor (glass::y (bounds, glass::bottom)));
    const auto edge = compact ? glass::pairedHubs[0] + glass::hubRadius
                              : glass::inset (glass::row (bounds, static_cast<float> (floorPixel) - static_cast<float> (line) * 0.2f));
    const auto right = compact ? glass::pairedHubs[1] - glass::hubRadius : glass::width - edge;
    auto plate = glass::band (bounds, edge, right, floorPixel - line, floorPixel).toFloat();
    const auto upper = plate.translated (0.0f, static_cast<float> (-line)); // the row above, at the small sizes
    if (view.facts.zero == lu_needle::Zero::noSong)
    {
        const auto text = text_style::shownText (view.vu.role == observatory::Role::post
            ? "CHOOSE A SONG IN B OR C" : "CHOOSE A SONG IN POST REFERENCE");
        const bool two = compact && text_style::shownWidth (font, text, text_style::LabelPolicy::fixed) > plate.getWidth();
        const auto rows = two ? metric_face::twoRows (text, font, plate.getWidth()) : std::array<juce::String, 2> { juce::String(), text };
        if (two) drawFixed (g, rows[0], upper, context, TextRole::legend, COL_NORMAL, juce::Justification::centred, trace);
        drawFixed (g, rows[1], plate, context, TextRole::legend, COL_NORMAL, juce::Justification::centred, trace);
        return;
    }
    const bool delta = ! compact && std::isfinite (view.reading.postMinusPre);
    const float width = plate.getWidth();
    const auto name = compact ? upper : plate.removeFromLeft (width * 0.32f);
    drawFixed (g, "0 = " + displayableText (view.facts.song), name, context,
        TextRole::legend, ivory, compact ? juce::Justification::centred : juce::Justification::centredLeft, trace);
    if (view.facts.zero != lu_needle::Zero::ready)
    {
        g.setColour (COL_NORMAL); g.setFont (labelFont (context, TextRole::legend, instrument));
        text_style::drawText (g, "NOT MEASURED IN KIRIN OS", plate,
                              compact ? juce::Justification::centred : juce::Justification::centredRight, true);
        note (trace, "NOT MEASURED IN KIRIN OS", plate, font); return;
    }
    auto value = plate.removeFromLeft (compact ? plate.getWidth() : width * 0.18f);
    drawFigures (g, juce::String (view.facts.zeroLufs, 1) + " LUFS", value, context, TextRole::legend,
        COL_NORMAL, juce::Justification::centred, trace);
    if (compact) return;
    const auto mono = view.facts.playedOffsetLu > 0.005
        ? "MONO +" + juce::String (view.facts.playedOffsetLu, 1) : juce::String();
    if (view.facts.samePlace == lu_needle::SamePlace::noValueHere)
    {
        // Missing-reference guidance takes the comparison slot, not the mono-zero explanation.
        if (mono.isNotEmpty())
            drawFixed (g, mono, plate.removeFromLeft (std::ceil (font.getStringWidthFloat (mono)) + 6.0f),
                context, TextRole::legend, COL_NORMAL, juce::Justification::centredLeft, trace);
        g.setColour (COL_NORMAL); g.setFont (labelFont (context, TextRole::legend, instrument));
        text_style::drawText (g, "REF: NO VALUE HERE", plate, juce::Justification::centredRight, true);
        note (trace, "REF: NO VALUE HERE", plate, font);
        return;
    }
    // The legend keeps what it needs at the right, and POST - PRE stands before it.
    std::vector<LegendItem> items;
    if (std::isfinite (view.reading.samePlace)) items.push_back ({ "REF", Needle::samePlace });
    if (std::isfinite (view.reading.pre)) items.push_back ({ "PRE", Needle::pre });
    if (delta)
    {
        const auto text = juce::String ("POST - PRE") + "  " + lu_needle::signedLu (view.reading.postMinusPre) + " LU";
        const auto needed = std::ceil (font.getStringWidthFloat (text)) + 4.0f;
        const auto after = mono.isNotEmpty() ? std::ceil (font.getStringWidthFloat (mono)) + 4.0f : legendWidth (font, items);
        drawFixed (g, text, plate.removeFromLeft (juce::jmax (needed, juce::jmin (width * 0.30f, plate.getWidth() - after - font.getHeight() * 0.5f))),
            context, TextRole::legend, COL_NORMAL, juce::Justification::centredRight, trace);
        plate.removeFromLeft (font.getHeight() * 0.5f);
    }
    if (mono.isNotEmpty())
        drawFixed (g, mono, plate, context, TextRole::legend, COL_NORMAL, juce::Justification::centredRight, trace);
    else
        paintLegend (g, plate, items, context, trace);
}

}

juce::Colour needleColour (Needle needle) noexcept
{
    return needle == Needle::own ? ivory : needle == Needle::pre ? COL_SPECTRUM_PRE : COL_SPECTRUM_DELTA;
}

juce::Rectangle<int> dialBounds (juce::Rectangle<int> bounds)
{
    const auto face = needle_painter::faceBounds (bounds);
    return face.getSmallestIntegerContainer().getIntersection (bounds);
}

juce::Point<float> needlePoint (juce::Rectangle<int> bounds, float normalized, float reach, int channel)
{
    return along (dialGeometry (needle_painter::faceBounds (bounds), channel), normalized, reach);
}

void paintScale (juce::Graphics& g, juce::Rectangle<int> bounds, const State& state, std::vector<TextBox>* trace)
{
    const auto face = needle_painter::faceBounds (bounds);
    const auto width = face.getWidth();
    const bool compact = compactFace (bounds);
    for (int channel = 0; channel < 2; ++channel)
    {
    const auto dial = dialGeometry (face, channel);
    juce::Path arc;
    for (int step = 0; step <= 80; ++step)
    {
        const auto point = dialPoint (dial, step / 80.0f);
        if (step == 0) arc.startNewSubPath (point); else arc.lineTo (point);
    }
    g.setColour (ivory.withAlpha (0.10f));
    g.strokePath (arc, juce::PathStrokeType (scaled (4.5f, width)));
    g.setColour (ivory.withAlpha (0.90f));
    g.strokePath (arc, juce::PathStrokeType (scaled (2.35f, width)));

    for (int lu = static_cast<int> (lu_needle::scaleLowLu); lu <= static_cast<int> (lu_needle::scaleHighLu); ++lu)
    {
        const bool major = lu % 3 == 0 && lu != -15, zero = lu == 0;
        if (compact && lu % 3 != 0) continue;
        const auto position = lu_needle::normalized (lu);
        g.setColour (zero ? COL_FLORA_BR : ivory.withAlpha (major ? 0.98f : 0.76f));
        g.drawLine ({ dialPoint (dial, position), along (dial, position, zero ? 1.17f : major ? 1.13f : 1.065f) },
                    scaled (zero ? 3.6f : major ? 2.6f : 1.4f, width, zero ? 1.4f : 0.7f));
    }

    // Figures are placed in order of importance and one that would touch another is left out, so
    // the small sizes keep 0 and both ends and the large ones show every figure.
    const auto context = faceContext (state.presentation);
    const auto font = monoFont (context, TextRole::axis, instrument);
    constexpr std::array<int, 9> figures { 0, -18, 9, -9, 3, -3, 6, -6, -12 };
    std::vector<juce::Rectangle<float>> placed;
    for (const auto lu : figures)
    {
        const auto position = lu_needle::normalized (lu);
        const auto inner = dialPoint (dial, position);
        const auto edge = std::abs (position * 2.0f - 1.0f);
        const auto edgeLift = juce::jlimit (0.0f, 1.0f, (edge - 0.4f) / 0.6f);
        const juce::Point<float> centre (dial.pivot.x + (inner.x - dial.pivot.x) * (1.15f + edge * 0.05f),
                                         dial.pivot.y + (inner.y - dial.pivot.y) * (1.21f + edgeLift * edgeLift * 0.12f));
        const auto text = lu > 0 ? "+" + juce::String (lu) : juce::String (lu);
        const auto textWidth = std::ceil (tabularTextWidth (font, text)) + 2.0f;
        const juce::Rectangle<float> area (centre.x - textWidth * 0.5f, centre.y - font.getHeight() * 0.5f,
                                           textWidth, font.getHeight());
        bool free = true;
        for (const auto& other : placed) free = free && ! other.expanded (2.0f, 0.0f).intersects (area);
        if (! free) continue;
        placed.push_back (area);
        drawFigures (g, text, area, context, TextRole::axis, lu == 0 ? COL_FLORA_BR : ivory,
                     juce::Justification::centred, trace);
    }
    }
}

void paintLive (juce::Graphics& g, juce::Rectangle<int> bounds, const Frame& view, std::vector<TextBox>* trace)
{
    const auto face = needle_painter::faceBounds (bounds);
    const auto width = face.getWidth();
    const bool compact = compactFace (bounds);
    const auto dial = dialGeometry (face, 0);
    const auto right = dialGeometry (face, 1);
    paintBand (g, right, width, view.facts);
    paintNeedle (g, right, width, Needle::own, view.integrated);
    if (std::isfinite (view.reading.samePlace)) paintNeedle (g, dial, width, Needle::samePlace, view.samePlace);
    if (std::isfinite (view.reading.pre)) paintNeedle (g, dial, width, Needle::pre, view.pre);
    paintNeedle (g, dial, width, Needle::own, view.own);

    const auto context = faceContext (view.vu.presentation);
    const auto readout = monoFont (context, TextRole::secondaryValue, instrument);
    const std::array<double, 2> values { view.reading.own, view.integratedLu };
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto item = dialGeometry (face, channel);
        const auto label = channel == 0 ? lu_needle::windowLabel (view.window) : "I";
        drawFigures (g, juce::String (label) + " " + lu_needle::signedLu (values[channel]),
            { item.pivot.x - item.radiusX * 0.62f, item.pivot.y - face.getHeight() * 0.285f,
              item.radiusX * 1.24f, juce::jmax (readout.getHeight(), scaled (42.0f, width, 14.0f)) },
            context, TextRole::secondaryValue, std::isfinite (values[channel]) ? COL_NORMAL : COL_TEXT_SECONDARY,
            juce::Justification::centred, trace);
    }
    paintZeroText (g, bounds, view, compact, trace);
}

void paint (juce::Graphics& g, juce::Rectangle<int> bounds, const Frame& view, std::vector<TextBox>* trace)
{
    paintStatic (g, bounds, view.vu);
    paintScale (g, bounds, view.vu, trace);
    paintDynamic (g, bounds, view.vu);
    paintLive (g, bounds, view, trace);
}
}
