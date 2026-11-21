#include "HyphaMetricFacePainter.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"
#include <array>
#include <cmath>

namespace hypha::metric_face
{
namespace
{
constexpr auto instrument = typography::Composition::instrument;
const auto ivory = COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.34f);
const auto bandColour = COL_SPECTRUM_DELTA.interpolatedWith (BG, 0.52f);
struct Geometry { juce::Point<float> pivot; float radiusX, radiusY; };
Geometry geometry (juce::Rectangle<int> bounds, bool chrome = false)
{
    const auto area = bounds.toFloat();
    if (chrome)
    {
        const float footer = area.getHeight() < 130.0f ? 36.0f : area.getHeight() < 220.0f ? 48.0f : 88.0f;
        const auto pivotY = area.getBottom() - footer;
        const auto wantedRadius = juce::jmax (8.0f, pivotY - (area.getY() + area.getHeight() * 0.30f + 32.0f));
        // Tiny chrome keeps its labels in one row; reserve the complete 4 px needle glow.
        const auto radiusY = area.getHeight() < 130.0f
            ? juce::jmax (1.0f, juce::jmin (wantedRadius, (pivotY - area.getY() - 62.0f) / 1.06f))
            : wantedRadius;
        return {{area.getCentreX(), pivotY}, area.getWidth() * 0.40f, radiusY};
    }
    if (area.getHeight() < 130.0f)
        return {{area.getCentreX(), area.getBottom() - 32.0f}, area.getWidth() * 0.40f, juce::jmax (8.0f, area.getHeight() - 95.0f)};
    if (area.getHeight() < 220.0f)
    {
        const auto radiusY = juce::jmax (8.0f, area.getHeight() - 126.0f);
        return {{area.getCentreX(), area.getBottom() - 48.0f}, area.getWidth() * 0.40f, radiusY};
    }
    const auto radius = juce::jmax (1.0f, juce::jmin (area.getWidth() * 0.40f, area.getHeight() - 150.0f));
    return {{area.getCentreX(), area.getY() + 62.0f + radius}, radius, radius};
}
juce::Point<float> point (const Geometry& dial, float value, float reach)
{
    const auto angle = juce::MathConstants<float>::pi * ((value - 0.5f) * 2.0f / 3.0f);
    return dial.pivot + juce::Point<float> (std::sin (angle) * dial.radiusX, -std::cos (angle) * dial.radiusY) * reach;
}
void text (juce::Graphics& g, const juce::String& value, juce::Rectangle<float> area,
           presentation::Context context, juce::Colour colour, std::vector<TextBox>* trace,
           text_style::LabelPolicy policy = text_style::LabelPolicy::fixed)
{
    context.referenceCompact = true;
    g.setColour (colour);
    g.setFont (labelFont (context, typography::TextRole::legend, instrument));
    text_style::drawText (g, value, area, juce::Justification::centred, true, policy);
    if (trace) trace->push_back ({value, area, labelFont (context, typography::TextRole::legend, instrument).getHeight()});
}
juce::String number (double value)
{
    if (!std::isfinite (value)) return juce::String::charToString (0x2014);
    return (value > 0.0 ? "+" : "") + juce::String (value, 2);
}
void needle (juce::Graphics& g, const Geometry& dial, std::optional<float> position,
             juce::Colour colour, bool own)
{
    if (!position) return;
    const auto end = point (dial, *position, own ? 1.06f : 1.0f);
    if (own) {
        g.setColour (COL_FLORA_BR.withAlpha (0.20f));
        g.drawLine ({dial.pivot, end}, 4.0f);
    }
    g.setColour (colour);
    g.drawLine ({dial.pivot, end}, own ? 2.0f : 1.2f);
}
}

juce::Point<float> needlePoint (juce::Rectangle<int> bounds, float normalized, float reach, bool withVuChrome)
{ return point (geometry (bounds, withVuChrome), normalized, reach); }

void paint (juce::Graphics& g, juce::Rectangle<int> bounds, const metric_needle::Frame& frame,
            presentation::Context context, std::vector<TextBox>* trace, bool withVuChrome)
{
    if (trace) trace->clear();
    const juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (bounds);
    const auto area = bounds.toFloat();
    const auto dial = geometry (bounds, withVuChrome);
    const auto facts = metric_needle::reading (frame);
    const auto range = metric_needle::extent (frame.face);
    g.setColour (BG); g.fillRoundedRectangle (area, 6.0f);
    if (!withVuChrome && area.getHeight() >= 220.0f) text (g, metric_needle::label (frame.face), {area.getX() + 8, area.getY() + 4, area.getWidth() - 16, 22},
          context, COL_NORMAL, trace);
    if (std::isfinite (range))
    {
        if (facts.relativeBand)
        {
            const auto low = *metric_needle::normalized (frame.face, facts.relativeBand->low);
            const auto high = *metric_needle::normalized (frame.face, facts.relativeBand->high);
            juce::Path band;
            for (int step = 0; step <= 32; ++step) {
                const auto p = point (dial, low + (high - low) * static_cast<float> (step) / 32.0f, 0.94f);
                if (step == 0) band.startNewSubPath (p); else band.lineTo (p);
            }
            g.setColour (bandColour); g.strokePath (band, juce::PathStrokeType (5.0f));
        }
        juce::Path arc;
        for (int step = 0; step <= 48; ++step) {
            const auto p = point (dial, static_cast<float> (step) / 48.0f, 1.0f);
            if (step == 0) arc.startNewSubPath (p); else arc.lineTo (p);
        }
        g.setColour (ivory.withAlpha (0.7f)); g.strokePath (arc, juce::PathStrokeType (1.0f));
        const int divisions = 6;
        for (int tick = 0; tick <= divisions; ++tick)
        {
            const auto fraction = static_cast<float> (tick) / static_cast<float> (divisions);
            const bool zero = tick == divisions / 2;
            g.setColour (zero ? COL_FLORA_BR : ivory);
            g.drawLine ({point (dial, fraction, 0.94f), point (dial, fraction, 1.0f)}, 1.2f);
            const auto p = point (dial, fraction, 1.0f);
            const auto value = -range + static_cast<double> (tick) * 2.0 * range / divisions;
            const int decimals = frame.face == metric_needle::Face::sharp ? 1 : 0;
            const auto label = zero ? juce::String ("0")
                : (value > 0.0 ? "+" : "") + juce::String (value, decimals);
            auto tickContext = context; tickContext.referenceCompact = true;
            const auto tickWidth = std::ceil (labelFont (tickContext, typography::TextRole::legend, instrument)
                .getStringWidthFloat (label)) + 2.0f;
            // Account for the entire swept needle, not just the needle at this tick.
            // A neighbouring angle reaches higher under the inward edge of a wide label.
            const auto inwardX = juce::jmax (0.0f, std::abs (p.x - dial.pivot.x) - tickWidth * 0.5f - 2.0f);
            const auto ratio = juce::jlimit (0.0f, 1.0f, inwardX / (dial.radiusX * 1.06f));
            const auto sweptTop = dial.pivot.y - dial.radiusY * 1.06f * std::sqrt (1.0f - ratio * ratio);
            const auto tickY = withVuChrome && area.getHeight() < 130.0f ? area.getY() + 41.0f
                : juce::jmin (p.y - 29.0f, sweptTop - 21.0f);
            text (g, label, {p.x - tickWidth * 0.5f, tickY, tickWidth, 18}, context,
                  zero ? COL_FLORA_BR : ivory, trace);
        }
    }
    // Keep exact values in the readout; a cap at the affected end indicates a clamped needle.
    for (auto direction : {-1, 1})
        if (metric_needle::rangeEdge (frame.face, facts.own) == direction
            || metric_needle::rangeEdge (frame.face, facts.pre) == direction
            || metric_needle::rangeEdge (frame.face, facts.reference) == direction)
        {
            const auto end = point (dial, direction < 0 ? 0.0f : 1.0f, 1.0f);
            g.setColour (COL_FLORA_BR);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (end));
        }
    needle (g, dial, facts.referencePosition, COL_SPECTRUM_DELTA, false);
    needle (g, dial, facts.prePosition, COL_SPECTRUM_PRE, false);
    needle (g, dial, facts.ownPosition, ivory, true);
    g.setColour (ivory); g.fillEllipse (dial.pivot.x - 3, dial.pivot.y - 3, 6, 6);
    const auto bottom = area.getBottom();
    const bool compact = area.getHeight() < 220.0f;
    const bool tiny = area.getHeight() < 130.0f;
    const auto width = (area.getWidth() - 16) / 3.0f;
    const std::array<juce::String, 3> values {
        juce::String (frame.ownIsPre ? "PRE " : "POST ") + number (facts.own),
        frame.ownIsPre ? juce::String {} : "PRE " + number (facts.pre), "REF " + number (facts.reference)};
    const std::array<juce::Colour, 3> colours {ivory, COL_SPECTRUM_PRE, COL_SPECTRUM_DELTA};
    for (std::size_t index = 0; index < values.size(); ++index)
        if (values[index].isNotEmpty())
            text (g, values[index], {area.getX() + 8 + width * static_cast<float> (index), bottom - (tiny ? 31.0f : compact ? 42.0f : 78.0f), width, tiny ? 15.0f : 20.0f},
                  context, colours[index], trace);
    juce::String status;
    const bool unavailable = frame.status != metric_needle::Status::ready || !std::isfinite (frame.median);
    if (frame.status == metric_needle::Status::measuring) status = text_style::shownText ("PREPARING");
    else if (unavailable && !frame.reason.empty()) status = text_style::shownText (metric_needle::reasonText (frame.reason));
    else if (frame.status != metric_needle::Status::ready || !std::isfinite (frame.median)) status = text_style::shownText ("UNAVAILABLE");
    else status = "0 = " + juce::String (frame.median, 2) + " " + metric_needle::unit (frame.face);
    if (compact) status = juce::String (metric_needle::label (frame.face)) + "  " + status
        + (unavailable && !frame.reason.empty() ? juce::String {} : "  " + juce::String::fromUTF8 (frame.song.c_str()));
    text (g, status, {area.getX() + 8, bottom - (tiny ? 15.0f : compact ? 22.0f : 55.0f), area.getWidth() - 16, tiny ? 15.0f : 20.0f}, context, COL_NORMAL, trace, text_style::LabelPolicy::fixed);
    if (!compact) text (g, juce::String::fromUTF8 (frame.song.c_str()), {area.getX() + 8, bottom - 30, area.getWidth() - 16, 22},
          context, COL_TEXT_SECONDARY, trace);
}
}
