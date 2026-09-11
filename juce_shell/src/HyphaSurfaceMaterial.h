#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"

namespace hypha::surface_material
{
inline juce::Colour graphiteEdge() noexcept
{
    return COL_MUTED.interpolatedWith (COL_NORMAL, 0.18f);
}

inline void paintPanel (juce::Graphics& g,
                        juce::Rectangle<float> area,
                        float fillAlpha,
                        float corner = 4.0f)
{
    if (area.isEmpty())
        return;

    const auto outer = area.reduced (0.5f);
    const auto radius = juce::jlimit (1.0f, juce::jmin (outer.getWidth(), outer.getHeight()) * 0.5f,
                                      corner);
    g.setColour (BG.brighter (0.055f).withAlpha (fillAlpha));
    g.fillRoundedRectangle (outer, radius);

    g.setColour (graphiteEdge().withAlpha (0.46f));
    g.drawRoundedRectangle (outer, radius, 0.75f);
    if (outer.getWidth() > 8.0f && outer.getHeight() > 7.0f)
    {
        const auto inner = outer.reduced (1.15f);
        g.setColour (BG.darker (0.28f).withAlpha (0.84f));
        g.drawRoundedRectangle (inner, juce::jmax (1.0f, radius - 1.0f), 0.65f);

        // A single continuous upper reflection gives the recessed instrument window depth.
        // It is deliberately structural and never follows a measurement or connection state.
        g.setColour (COL_NORMAL.withAlpha (0.075f));
        g.drawLine (inner.getX() + radius,
                    inner.getY() + 0.35f,
                    inner.getRight() - radius,
                    inner.getY() + 0.35f,
                    0.65f);
    }
}

inline void paintControl (juce::Graphics& g,
                          juce::Rectangle<float> area,
                          bool highlighted,
                          bool down,
                          bool selected,
                          juce::Colour accent = COL_SPECTRUM_DELTA_BR,
                          float corner = 3.0f)
{
    if (area.isEmpty())
        return;

    const auto fill = selected ? accent.interpolatedWith (BG, 0.82f)
                               : down ? kFieldFill.brighter (0.08f)
                                      : kFieldFill;
    paintPanel (g, area, highlighted ? 0.94f : selected ? 0.88f : 0.74f, corner);
    g.setColour (fill.withAlpha (down ? 0.62f : selected ? 0.42f : 0.18f));
    g.fillRoundedRectangle (area.reduced (1.35f), juce::jmax (1.0f, corner - 0.8f));
    const auto edge = selected ? accent
                               : highlighted ? accent.interpolatedWith (graphiteEdge(), 0.68f)
                                             : graphiteEdge();
    g.setColour (edge
                     .withAlpha (selected ? 0.76f : highlighted ? 0.52f : 0.30f));
    g.drawRoundedRectangle (area.reduced (0.7f), corner, selected ? 0.95f : 0.65f);
}

inline void paintInstrumentFrame (juce::Graphics& g,
                                  juce::Rectangle<float> area,
                                  bool capture)
{
    if (area.isEmpty())
        return;

    const auto outer = area.reduced (0.75f);
    const auto corner = capture ? 7.0f : 5.0f;
    g.setColour (graphiteEdge().withAlpha (capture ? 0.70f : 0.58f));
    g.drawRoundedRectangle (outer, corner, capture ? 1.15f : 0.85f);
    g.setColour (BG.darker (0.34f).withAlpha (0.92f));
    g.drawRoundedRectangle (outer.reduced (1.35f), corner - 1.0f, 0.75f);
    g.setColour (COL_NORMAL.withAlpha (capture ? 0.10f : 0.080f));
    g.drawLine (outer.getX() + corner,
                outer.getY() + 0.55f,
                outer.getRight() - corner,
                outer.getY() + 0.55f,
                capture ? 0.9f : 0.7f);
    g.setColour (COL_FLORA.withAlpha (capture ? 0.16f : 0.095f));
    g.drawLine (outer.getX() + outer.getWidth() * 0.37f,
                outer.getY() + 0.55f,
                outer.getX() + outer.getWidth() * 0.63f,
                outer.getY() + 0.55f,
                capture ? 0.8f : 0.6f);
}

inline void paintObservationWell (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (area.isEmpty())
        return;

    g.setColour (BG.darker (0.48f));
    g.fillRect (area);

    const auto inner = area.reduced (0.35f);
    g.setColour (graphiteEdge().withAlpha (0.34f));
    g.drawRect (inner, 0.70f);
    g.setColour (COL_NORMAL.withAlpha (0.045f));
    g.drawLine (inner.getX() + 1.0f, inner.getY() + 0.40f,
                inner.getRight() - 1.0f, inner.getY() + 0.40f, 0.60f);
    g.setColour (BG.darker (0.82f).withAlpha (0.88f));
    g.drawLine (inner.getX() + 1.0f, inner.getBottom() - 0.35f,
                inner.getRight() - 1.0f, inner.getBottom() - 0.35f, 0.65f);
}
}
