#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"

// Hypha depth material (2.5D), shared by every page. The light follows the page's composition, as
// on the VU chassis (2026-09-29, Daisuke): each page has one main observation window, and only it
// catches the light from above, on its cut edge (brightest along the top, a warm bounce along the
// bottom). No light is placed the same on every window: a spot at each upper corner of every
// window read as ornament, not as light (Daisuke). Cards, panels and lanes stay quiet: a
// fine outline and the shadow of their upper wall. The glass itself never holds a reflection
// shape, so no line crosses a surface. Controls are raised plates (a lit upper bevel, a shaded
// lower edge and a soft contact shadow). Material only: nothing here follows a measured value, so
// every caller may draw it once into a cached image.
namespace hypha::depth_material
{
struct WellLight
{
    float shadow = 0.0f;   // inner shadow along the upper and left walls
    float vignette = 0.0f; // darker right end and floor
    float edge = 0.0f;     // the page's main window only: the key light on its cut edge
    float outline = 0.0f;  // a quiet well: its fine outline
};

// The main window's cut edge, a bevel a few pixels wide as on the VU chassis: a dark outer line,
// the lit face of the bevel and a fine ivory lip where the glass begins, and a dark band just
// inside that sets the glass back.
inline void paintMainEdge (juce::Graphics& g, juce::Rectangle<float> area, float radius, float k)
{
    const auto black = juce::Colours::black;
    const auto warm = juce::Colour (0xffe0ad62); // the VU chassis rim
    // Diffuse light falling from above onto the glass: no edge anywhere.
    const auto fall = area.getHeight() * 0.38f;
    g.setGradientFill ({ COL_NORMAL.withAlpha (0.030f * k), 0.0f, area.getY(),
                         COL_NORMAL.withAlpha (0.0f), 0.0f, area.getY() + fall, false });
    g.fillRect (area.withHeight (fall));
    juce::Path inner;
    inner.addRoundedRectangle (area.reduced (2.2f), juce::jmax (0.0f, radius - 1.0f));
    g.setColour (black.withAlpha (0.42f * k));
    g.strokePath (inner, juce::PathStrokeType (3.2f));
    juce::Path outer;
    outer.addRoundedRectangle (area.reduced (0.6f), radius);
    g.setColour (black.withAlpha (0.55f * k));
    g.strokePath (outer, juce::PathStrokeType (1.4f));
    // Bright along the top, dim down the sides, a warm bounce at the bottom.
    juce::Path bevel;
    bevel.addRoundedRectangle (area.reduced (2.0f), juce::jmax (0.0f, radius - 1.2f));
    juce::ColourGradient lit (warm.withAlpha (0.78f * k), 0.0f, area.getY(),
                              warm.withAlpha (0.42f * k), 0.0f, area.getBottom(), false);
    lit.addColour (0.12, warm.withAlpha (0.40f * k));
    lit.addColour (0.55, warm.withAlpha (0.10f * k));
    lit.addColour (0.88, warm.withAlpha (0.16f * k));
    g.setGradientFill (lit);
    g.strokePath (bevel, juce::PathStrokeType (2.2f));
    juce::Path lip;
    lip.addRoundedRectangle (area.reduced (3.4f), juce::jmax (0.0f, radius - 2.4f));
    juce::ColourGradient lipLight (COL_NORMAL.withAlpha (0.36f * k), 0.0f, area.getY(),
                                   COL_NORMAL.withAlpha (0.10f * k), 0.0f, area.getBottom(), false);
    lipLight.addColour (0.3, COL_NORMAL.withAlpha (0.04f * k));
    g.setGradientFill (lipLight);
    g.strokePath (lip, juce::PathStrokeType (0.7f));
    const auto bounce = juce::jlimit (3.0f, 10.0f, area.getHeight() * 0.08f);
    g.setGradientFill ({ warm.withAlpha (0.0f), 0.0f, area.getBottom() - bounce,
                         warm.withAlpha (0.10f * k), 0.0f, area.getBottom(), false });
    g.fillRect (area.withTop (area.getBottom() - bounce));
}

inline void paintRecessedWell (juce::Graphics& g, juce::Rectangle<float> area, float radius,
                               const WellLight& light)
{
    if (area.getWidth() < 6.0f || area.getHeight() < 6.0f
        || (light.shadow <= 0.0f && light.vignette <= 0.0f && light.edge <= 0.0f
            && light.outline <= 0.0f))
        return;
    juce::Graphics::ScopedSaveState saved (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, radius);
    g.reduceClipRegion (clip);
    const auto black = juce::Colours::black;
    if (light.shadow > 0.0f)
    {
        // The upper and left walls face away from the light.
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
    if (light.edge > 0.0f)
    {
        paintMainEdge (g, area, radius, juce::jmin (1.0f, light.edge));
        return;
    }
    if (light.outline > 0.0f)
    {
        juce::Path outline;
        outline.addRoundedRectangle (area.reduced (0.5f), radius);
        g.setColour (COL_NORMAL.withAlpha (light.outline));
        g.strokePath (outline, juce::PathStrokeType (1.0f));
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

// A page's main observation window outside DRUM (the plot of FREQ, LIVE, SHARP and the like):
// the one surface the light lands on.
inline WellLight observationWellLight() noexcept
{
    return { 0.66f, 0.32f, 1.0f, 0.0f };
}

// Measurement cards and section panels: quiet, the shadow scaled by how opaque the panel is.
inline WellLight panelWellLight (float strength) noexcept
{
    return { 0.62f * strength, 0.0f, 0.0f, strength > 0.0f ? 0.10f : 0.0f };
}
}
