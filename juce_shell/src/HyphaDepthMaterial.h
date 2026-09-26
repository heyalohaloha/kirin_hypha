#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"

// Hypha depth material (2.5D), shared by every page. One key light sits at the upper left, so
// every surface agrees: observation windows are recessed glass (their upper and left walls face
// away from the light, their lower rims catch it), and cards and controls are raised plates (a
// lit upper bevel, a shaded lower edge and a soft contact shadow). Material only: nothing here
// follows a measured value, so every caller may draw it once into a cached image.
namespace hypha::depth_material
{
struct WellLight
{
    float shadow = 0.0f;   // inner shadow along the upper and left walls
    float rim = 0.0f;      // bounce light along the lower and right rims
    float sheen = 0.0f;    // still reflection on the glass cover
    float vignette = 0.0f; // darker right end and floor
    float lip = 0.0f;      // the upper edge of the cut catching the key light
    juce::Colour rimColour;
};

inline void paintRecessedWell (juce::Graphics& g, juce::Rectangle<float> area, float radius,
                               const WellLight& light)
{
    if (area.getWidth() < 6.0f || area.getHeight() < 6.0f
        || (light.shadow <= 0.0f && light.rim <= 0.0f && light.sheen <= 0.0f
            && light.vignette <= 0.0f && light.lip <= 0.0f))
        return;
    juce::Graphics::ScopedSaveState saved (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, radius);
    g.reduceClipRegion (clip);
    const auto black = juce::Colours::black;
    if (light.shadow > 0.0f)
    {
        // The upper and left walls face away from the key light.
        const auto drop = juce::jlimit (3.0f, 12.0f, area.getHeight() * 0.16f);
        g.setGradientFill ({ black.withAlpha (light.shadow), 0.0f, area.getY(),
                             black.withAlpha (0.0f), 0.0f, area.getY() + drop, false });
        g.fillRect (area.withHeight (drop));
        const auto side = juce::jlimit (2.0f, 8.0f, area.getWidth() * 0.02f);
        g.setGradientFill ({ black.withAlpha (light.shadow * 0.6f), area.getX(), 0.0f,
                             black.withAlpha (0.0f), area.getX() + side, 0.0f, false });
        g.fillRect (area.withWidth (side));
    }
    if (light.vignette > 0.0f)
    {
        const auto reach = area.getWidth() * 0.12f;
        g.setGradientFill ({ black.withAlpha (0.0f), area.getRight() - reach, 0.0f,
                             black.withAlpha (light.vignette * 0.6f), area.getRight(), 0.0f, false });
        g.fillRect (area.withLeft (area.getRight() - reach));
        const auto low = area.getHeight() * 0.24f;
        g.setGradientFill ({ black.withAlpha (0.0f), 0.0f, area.getBottom() - low,
                             black.withAlpha (light.vignette * 0.5f), 0.0f, area.getBottom(), false });
        g.fillRect (area.withTop (area.getBottom() - low));
    }
    if (light.lip > 0.0f)
    {
        // Depth lives on the edges, never in a lit centre: the upper edge of the cut catches
        // the key light as one fine ivory line above the shadowed wall.
        g.setGradientFill ({ COL_NORMAL.withAlpha (light.lip), area.getX(), 0.0f,
                             COL_NORMAL.withAlpha (light.lip * 0.35f), area.getRight(), 0.0f, false });
        g.fillRect (juce::Rectangle<float> (area.getX() + radius, area.getY() + 1.1f,
                                            area.getWidth() - 2.0f * radius, 0.9f));
    }
    if (light.sheen > 0.0f)
    {
        // A still reflection on the glass cover, falling away before the centre of the well.
        juce::Path band;
        band.startNewSubPath (area.getX(), area.getY());
        band.lineTo (area.getX() + area.getWidth() * 0.62f, area.getY());
        band.lineTo (area.getX() + area.getWidth() * 0.34f, area.getBottom());
        band.lineTo (area.getX(), area.getBottom());
        band.closeSubPath();
        g.setGradientFill ({ COL_NORMAL.withAlpha (light.sheen), area.getX(), area.getY(),
                             COL_NORMAL.withAlpha (0.0f), area.getX() + area.getWidth() * 0.36f,
                             area.getBottom(), false });
        g.fillPath (band);
    }
    if (light.rim > 0.0f)
    {
        // The lower and right walls catch the light and bounce a little of it back up.
        const auto rim = light.rimColour;
        const auto bounce = juce::jlimit (2.0f, 8.0f, area.getHeight() * 0.08f);
        g.setGradientFill ({ rim.withAlpha (0.0f), 0.0f, area.getBottom() - bounce,
                             rim.withAlpha (light.rim * 0.35f), 0.0f, area.getBottom(), false });
        g.fillRect (area.withTop (area.getBottom() - bounce));
        g.setColour (rim.withAlpha (light.rim));
        g.fillRect (juce::Rectangle<float> (area.getX() + radius, area.getBottom() - 1.0f,
                                            area.getWidth() - 2.0f * radius, 1.0f));
        // The cut glass edge: a fine ivory lip just above the lit lower rim.
        g.setColour (COL_NORMAL.withAlpha (light.rim * 0.55f));
        g.fillRect (juce::Rectangle<float> (area.getX() + radius * 2.0f, area.getBottom() - 2.0f,
                                            area.getWidth() - 4.0f * radius, 0.6f));
        g.setColour (rim.withAlpha (light.rim * 0.45f));
        g.fillRect (juce::Rectangle<float> (area.getRight() - 1.0f, area.getY() + radius,
                                            1.0f, area.getHeight() - 2.0f * radius));
    }
}

inline void paintCastShadowAbove (juce::Graphics& g, juce::Rectangle<float> area, float radius, float strength)
{
    if (strength <= 0.0f || area.getWidth() <= 2.0f * radius)
        return;
    g.setColour (juce::Colours::black.withAlpha (juce::jmin (1.0f, strength)));
    g.fillRect (juce::Rectangle<float> (area.getX() + radius, area.getY() - 1.0f,
                                        area.getWidth() - 2.0f * radius, 1.0f));
}

inline void paintRaisedPlate (juce::Graphics& g, juce::Rectangle<float> area, float radius, float strength)
{
    if (strength <= 0.0f || area.getWidth() < 8.0f || area.getHeight() < 7.0f)
        return;
    const auto black = juce::Colours::black;
    // A soft contact shadow just below the plate, where it meets the face.
    const auto contact = juce::jlimit (1.5f, 3.0f, area.getHeight() * 0.06f);
    g.setGradientFill ({ black.withAlpha (0.45f * strength), 0.0f, area.getBottom(),
                         black.withAlpha (0.0f), 0.0f, area.getBottom() + contact, false });
    g.fillRect (juce::Rectangle<float> (area.getX() + radius, area.getBottom(),
                                        area.getWidth() - 2.0f * radius, contact));
    juce::Graphics::ScopedSaveState saved (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, radius);
    g.reduceClipRegion (clip);
    // The lower part of the plate turns away from the key light.
    const auto shade = juce::jlimit (3.0f, 14.0f, area.getHeight() * 0.22f);
    g.setGradientFill ({ black.withAlpha (0.0f), 0.0f, area.getBottom() - shade,
                         black.withAlpha (0.34f * strength), 0.0f, area.getBottom(), false });
    g.fillRect (area.withTop (area.getBottom() - shade));
    // The upper bevel catches it: a fine ivory edge, brightest toward the left.
    g.setGradientFill ({ COL_NORMAL.withAlpha (0.30f * strength), area.getX(), 0.0f,
                         COL_NORMAL.withAlpha (0.06f * strength), area.getRight(), 0.0f, false });
    g.fillRect (juce::Rectangle<float> (area.getX() + radius, area.getY() + 0.5f,
                                        area.getWidth() - 2.0f * radius, 0.8f));
}

// The shared light for observation windows outside DRUM (plots such as FREQ, LIVE and SHARP).
inline WellLight observationWellLight() noexcept
{
    return { 0.66f, 0.30f, 0.050f, 0.32f, 0.16f, COL_FLORA };
}

// Measurement cards and section panels: the same recessed glass without the plot's vignette,
// scaled by how opaque the panel is.
inline WellLight panelWellLight (float strength) noexcept
{
    return { 0.62f * strength, 0.30f * strength, 0.050f * strength, 0.0f, 0.24f * strength, COL_FLORA };
}
}
