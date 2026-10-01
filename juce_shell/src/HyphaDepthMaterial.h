#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"

// Hypha depth material (2.5D), shared by every page. The light follows the page's composition, as
// on the VU chassis (2026-09-29, Daisuke): each page has one main window, and only its frame
// catches the editor's one key light (HyphaMainFrame.h, 2026-10-01). No light is placed the same
// on every window: a spot at each upper corner of every window read as ornament, not as light
// (Daisuke). Wells here are glass: the shadow of their upper wall and, for a quiet card, panel or
// lane, a fine outline. The glass never holds a reflection shape, so no line crosses a surface.
// Controls are raised plates (a lit upper bevel, a shaded lower edge and a soft contact shadow).
// Material only: nothing here follows a measured value, so every caller may draw it once into a
// cached image.
namespace hypha::depth_material
{
struct WellLight
{
    float shadow = 0.0f;   // inner shadow along the upper and left walls
    float vignette = 0.0f; // darker right end and floor
    float outline = 0.0f;  // a quiet well: its fine outline
};

inline void paintRecessedWell (juce::Graphics& g, juce::Rectangle<float> area, float radius,
                               const WellLight& light)
{
    if (area.getWidth() < 6.0f || area.getHeight() < 6.0f
        || (light.shadow <= 0.0f && light.vignette <= 0.0f && light.outline <= 0.0f))
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

// `landing`: where along its upper edge the key light lands (0 left, 1 right; HyphaKeyLight.h).
inline void paintRaisedPlate (juce::Graphics& g, juce::Rectangle<float> area, float radius, float strength,
                              float landing)
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
    // The upper bevel catches it: a fine ivory edge, brightest where the light lands.
    const auto lit = [landing, strength] (float at) {
        return COL_NORMAL.withAlpha ((0.06f + 0.24f * juce::jmax (0.0f, 1.0f - 1.6f * std::abs (at - landing))) * strength); };
    juce::ColourGradient bevel (lit (0.0f), area.getX(), 0.0f, lit (1.0f), area.getRight(), 0.0f, false);
    if (landing > 0.02f && landing < 0.98f)
        bevel.addColour (static_cast<double> (landing), lit (landing));
    g.setGradientFill (bevel);
    g.fillRect (juce::Rectangle<float> (area.getX() + radius, area.getY() + 0.5f,
                                        area.getWidth() - 2.0f * radius, 0.8f));
}

// A page's main observation window outside DRUM (the plot of FREQ, LIVE, SHARP and the like): its
// glass. The light lands on its frame (HyphaMainFrame.h), not inside it.
inline WellLight observationWellLight() noexcept
{
    return { 0.66f, 0.32f, 0.0f };
}

// Measurement cards and section panels: quiet, the shadow scaled by how opaque the panel is.
inline WellLight panelWellLight (float strength) noexcept
{
    return { 0.62f * strength, 0.0f, strength > 0.0f ? 0.10f : 0.0f };
}
}
