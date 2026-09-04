#include "HyphaPsbPainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <cmath>

namespace hypha::psb_painter
{
namespace
{
float visualScale (juce::Rectangle<float> bounds) noexcept
{
    return spectrum_geometry::visualScaleFor (bounds);
}

float niceCeiling (const State& state) noexcept
{
    double peak = 0.0;
    for (const auto value : state.values)
        peak = std::max (peak, state.delta ? std::abs (value) : value);
    const double floor = state.delta ? 0.02 : 0.10;
    const double step = state.delta ? 0.01 : 0.05;
    return (float) std::min (1.0, std::max (floor, std::ceil (peak / step) * step));
}

juce::String valueText (double value, bool delta)
{
    const auto scaled = value * 100.0;
    return (delta && scaled >= 0.0 ? "+" : "") + juce::String (scaled, 1)
         + (delta ? " PP" : "%");
}
}
juce::Rectangle<float> dataBounds (juce::Rectangle<float> bounds)
{
    const auto scale = visualScale (bounds);
    return spectrum_geometry::plotBoundsFor (bounds)
        .withTrimmedTop (20.0f * scale)
        .withTrimmedBottom (8.0f * scale);
}

int bandAt (juce::Rectangle<float> bounds, juce::Point<float> point) noexcept
{
    const auto plot = dataBounds (bounds);
    if (! plot.contains (point)) return -1;
    return juce::jlimit (0, (int) bandCount - 1,
        (int) ((point.x - plot.getX()) * (float) bandCount / plot.getWidth()));
}

void paintSubviewToggle (juce::Graphics& g, juce::Rectangle<float> bounds, bool psbSelected)
{
    const auto scale = visualScale (bounds);
    const auto button = spectrum_geometry::subviewBoundsFor (
        spectrum_geometry::plotBoundsFor (bounds), scale);
    g.setColour ((psbSelected ? COL_LED_BLUE : COL_MUTED).withAlpha (
        psbSelected ? 0.14f : 0.05f));
    g.fillRoundedRectangle (button, 3.0f * scale);
    g.setColour ((psbSelected ? COL_LED_BLUE : COL_MUTED).withAlpha (
        psbSelected ? 0.88f : 0.48f));
    g.drawRoundedRectangle (button, 3.0f * scale, 0.75f * scale);
    g.setFont (monoFont (7.0f * ui_contract::analysisTextScale (scale)));
    g.drawFittedText (psbSelected ? "SPECTRUM" : "PSB", button.toNearestInt(),
                      juce::Justification::centred, 1, 0.65f);
}

void paint (juce::Graphics& g, juce::Rectangle<float> bounds, const State& state)
{
    const auto scale = visualScale (bounds);
    const auto outer = spectrum_geometry::plotBoundsFor (bounds);
    const auto plot = dataBounds (bounds);
    g.setColour (BG.withAlpha (0.84f));
    g.fillRoundedRectangle (outer, 4.0f * scale);

    g.setFont (monoFont (8.0f * ui_contract::analysisTextScale (scale)));
    g.setColour (COL_NORMAL.withAlpha (0.86f));
    g.drawText (state.delta ? "PSB / POST - PRE SHARE" : "PSB / POST SHARE",
                outer.withHeight (18.0f * scale).toNearestInt(), juce::Justification::centredLeft);
    if (! state.available)
    {
        g.setColour (COL_MUTED.withAlpha (0.72f));
        g.drawText ("PSB -- WARMING", plot.toNearestInt(), juce::Justification::centred);
        return;
    }

    const auto ceiling = niceCeiling (state);
    const auto baseline = state.delta ? plot.getCentreY() : plot.getBottom();
    g.setColour (COL_MUTED.withAlpha (0.15f));
    for (int line = 0; line <= 4; ++line)
    {
        const auto y = plot.getY() + plot.getHeight() * (float) line / 4.0f;
        g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());
    }
    g.setColour (COL_NORMAL.withAlpha (0.32f));
    g.drawHorizontalLine ((int) std::round (baseline), plot.getX(), plot.getRight());

    const auto slot = plot.getWidth() / (float) bandCount;
    const auto colour = state.delta ? COL_SPECTRUM_DELTA : COL_SPECTRUM_POST;
    for (std::size_t index = 0; index < bandCount; ++index)
    {
        const auto value = state.values[index];
        const auto amount = (float) juce::jlimit (0.0, (double) ceiling, std::abs (value));
        const auto availableHeight = state.delta ? plot.getHeight() * 0.5f : plot.getHeight();
        const auto height = availableHeight * amount / ceiling;
        const auto x = plot.getX() + slot * (float) index;
        const auto y = state.delta && value >= 0.0 ? baseline - height
                     : state.delta ? baseline : baseline - height;
        auto bar = juce::Rectangle<float> (x + slot * 0.16f, y,
                                           slot * 0.68f, std::max (1.0f, height));
        const bool hovered = (int) index == state.hoverBand;
        g.setColour (colour.withAlpha (hovered ? 0.92f : 0.58f));
        g.fillRoundedRectangle (bar, std::min (2.0f * scale, bar.getWidth() * 0.25f));
    }

    g.setFont (monoFont (7.0f * ui_contract::analysisTextScale (scale)));
    g.setColour (COL_MUTED.withAlpha (0.68f));
    g.drawText (state.delta ? "+/- " + juce::String (ceiling * 100.0f, 0) + " PP"
                            : "0 - " + juce::String (ceiling * 100.0f, 0) + "%",
                outer.withTrimmedTop (18.0f * scale).withHeight (12.0f * scale).toNearestInt(),
                juce::Justification::centredRight);
    for (const int band : { 1, 5, 10, 15, 20 })
    {
        const auto x = plot.getX() + slot * ((float) band - 0.5f);
        g.drawText (juce::String (band), juce::Rectangle<float> (x - 12.0f * scale,
                    plot.getBottom(), 24.0f * scale, 10.0f * scale).toNearestInt(),
                    juce::Justification::centred);
    }
    if (state.hoverBand >= 0 && state.hoverBand < (int) bandCount)
    {
        const auto text = juce::String ("B") + juce::String (state.hoverBand + 1) + "  "
                        + valueText (state.values[(std::size_t) state.hoverBand], state.delta);
        auto readout = plot;
        readout = readout.removeFromTop (18.0f * scale).removeFromRight (100.0f * scale);
        g.setColour (BG.brighter (0.1f).withAlpha (0.96f));
        g.fillRoundedRectangle (readout, 3.0f * scale);
        g.setColour (colour);
        g.drawText (text, readout.toNearestInt(), juce::Justification::centred);
    }
}
}
