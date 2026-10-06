#pragma once

#include <cmath>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaKeyLight.h"
#include "HyphaMainFrameGeometry.h"
#include "HyphaMaterialCache.h"

// The frame of a page's main window (2026-10-01, stage 2 of the light): the VU chassis's bronze
// bevel around the window's glass, lit by the editor's one key light (HyphaKeyLight.h). The frame
// stands outside the window, so it never covers what the window shows. Only the main window of a
// page has it; cards, panels and lanes stay quiet (HyphaDepthMaterial.h).
namespace hypha::main_frame
{
using Measures = main_frame_geometry::Measures;

// As on the VU chassis at 900 x 600 (a 7 px ring), and never thinner than a bevel can show. A
// magnified editor scales the 900 x 600 layout, so the frame scales with it.
inline Measures measuresFor (const key_light::Light& light) noexcept
{
    return main_frame_geometry::forDiagonal (light.diagonal);
}

// What the frame and its shadow cover around a window: the shadow's widest stroke, shifted down.
inline float marginFor (const Measures& measures) noexcept
{
    return static_cast<float> (main_frame_geometry::marginFor (measures));
}

// How far the shadow of the glass's upper wall reaches into the window.
inline float topShadowOf (juce::Rectangle<float> window, const Measures& measures) noexcept
{
    return juce::jmin (3.0f * measures.ring, window.getHeight() * 0.12f);
}

namespace uncached
{
inline void paint (juce::Graphics& g, juce::Rectangle<float> window, key_light::Landing landing,
                   const Measures& measures)
{
    const auto black = juce::Colours::black;
    const auto gold = juce::Colour (0xffe0ad62); // the VU chassis rim
    const auto outer = window.expanded (measures.ring);
    const auto outerRadius = measures.radius + measures.ring;
    const auto k = landing.strength;
    const juce::Point<float> lit { outer.getX() + landing.fraction * outer.getWidth(), outer.getY() };
    const auto reach = juce::jmax (outer.getWidth(), outer.getHeight()) * 0.75f;
    // The frame stands a little off the chassis: a soft shadow around it, a little deeper below.
    // Two bands, so a cached image composes them as the direct paint does.
    for (const auto [spread, alpha] : { std::pair { 0.35f, 0.24f }, std::pair { 0.85f, 0.10f } })
    {
        const auto offset = spread * measures.shadow;
        g.setColour (black.withAlpha (alpha));
        g.drawRoundedRectangle (outer.expanded (offset).translated (0.0f, 0.35f * offset),
                                outerRadius + offset, 0.5f * measures.shadow);
    }
    // The bevel: dark bronze, lit where the light lands and fading along the frame from there.
    juce::Path bevel;
    bevel.setUsingNonZeroWinding (false);
    bevel.addRoundedRectangle (outer, outerRadius);
    bevel.addRoundedRectangle (window, measures.radius);
    g.setGradientFill ({ juce::Colour (0xff3a2a18), 0.0f, outer.getY(),
                         juce::Colour (0xff1c140b), 0.0f, outer.getBottom(), false });
    g.fillPath (bevel);
    g.setColour (black.withAlpha (0.85f));
    g.drawRoundedRectangle (outer.reduced (0.5f), outerRadius, 1.2f);
    // The light falls along the top edge from where it lands, and down the side it lands nearest
    // when it lands at an end. Straight gradients over the few strips the light reaches: a radial
    // one over the whole frame cost several times the rest of the frame to paint.
    const auto spread = 0.55f * reach;
    const auto along = [&] (juce::Colour colour, float alpha, float x0, float x1) {
        juce::ColourGradient gradient (colour.withAlpha (0.0f), lit.x - spread, 0.0f, colour.withAlpha (0.0f),
                                       lit.x + spread, 0.0f, false);
        gradient.addColour (0.5, colour.withAlpha (alpha));
        g.setGradientFill (gradient);
        return juce::Rectangle<float>::leftTopRightBottom (x0, 0.0f, x1, 0.0f);
    };
    const auto bronzeLight = juce::Colour (0xff9a7440);
    const auto fall = juce::jmin (outer.getHeight(), spread * 0.6f);
    const auto atEnd = [&] (bool right) {
        const auto nearness = right ? landing.fraction : 1.0f - landing.fraction;
        return juce::jmax (0.0f, (nearness - 0.8f) / 0.2f);
    };
    {
        const juce::Graphics::ScopedSaveState onBevel (g);
        g.reduceClipRegion (bevel);
        const auto strip = along (bronzeLight, 0.75f * k, outer.getX(), outer.getRight());
        g.fillRect (strip.withTop (outer.getY()).withBottom (window.getY() + measures.radius));
        for (const bool right : { false, true })
            if (const auto end = atEnd (right); end > 0.0f)
            {
                g.setGradientFill ({ bronzeLight.withAlpha (0.75f * k * end), 0.0f, outer.getY(),
                                     bronzeLight.withAlpha (0.0f), 0.0f, outer.getY() + fall, false });
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (right ? window.getRight() : outer.getX(), outer.getY(),
                                                                        right ? outer.getRight() : window.getX(),
                                                                        outer.getY() + fall));
            }
    }
    // The inner lip where the glass begins: faint all round, bright where the light lands.
    g.setColour (gold.withAlpha (0.08f * k));
    g.drawRoundedRectangle (window.expanded (0.6f), measures.radius + 0.6f, 1.4f);
    {
        const auto strip = along (gold, 0.95f * k, window.getX(), window.getRight());
        g.fillRect (strip.withTop (window.getY() - 1.3f).withBottom (window.getY() + 0.1f));
        for (const bool right : { false, true })
            if (const auto end = atEnd (right); end > 0.0f)
            {
                const auto x = right ? window.getRight() - 0.1f : window.getX() - 1.3f;
                g.setGradientFill ({ gold.withAlpha (0.95f * k * end), 0.0f, window.getY(),
                                     gold.withAlpha (0.0f), 0.0f, window.getY() + fall, false });
                g.fillRect (juce::Rectangle<float> (x, window.getY(), 1.4f, juce::jmin (fall, window.getHeight())));
            }
    }
    // A little warm light bounced up onto the lower bevel, under where the light lands.
    juce::ColourGradient bounce (gold.withAlpha (0.0f), window.getX(), 0.0f,
                                 gold.withAlpha (0.0f), window.getRight(), 0.0f, false);
    bounce.addColour (juce::jlimit (0.1, 0.9, static_cast<double> (landing.fraction)), gold.withAlpha (0.28f * k));
    g.setGradientFill (bounce);
    g.fillRect (juce::Rectangle<float> (window.getX() + outerRadius, window.getBottom() + 0.6f,
                                        window.getWidth() - 2.0f * outerRadius, 1.2f));
    // The glass lies below the frame: its upper wall in shadow just inside.
    const auto drop = topShadowOf (window, measures);
    g.setGradientFill ({ black.withAlpha (0.38f), 0.0f, window.getY(),
                         black.withAlpha (0.0f), 0.0f, window.getY() + drop, false });
    g.fillRect (window.withHeight (drop));
}

// The frame around `window`, lit by the light of the component now painting, painted directly.
inline void paint (juce::Graphics& g, juce::Rectangle<float> window)
{
    const auto& light = key_light::current();
    const auto measures = measuresFor (light);
    paint (g, window, key_light::landingOn (window.expanded (measures.ring), light), measures);
}
}

// The ring a main window gives up to its frame where the frame has no room outside it: while
// laying out for `context`, or while painting.
inline int insetFor (const presentation::Context& context) noexcept
{
    return main_frame_geometry::insetFor (context);
}

inline int inset() noexcept
{
    return juce::roundToInt (measuresFor (key_light::current()).ring) + 1;
}

// The frame around `window`, lit by the light of the component now painting. Only the ring
// around the window and the shadow just inside its top hold anything, so the glass below is
// clipped away: a repaint composites the ring, not the whole window. The fine bevel uses the
// canonical raster from its first paint, so entering the cache never changes its antialiasing at
// fractional local or inherited positions. Without an editor the same raster is temporary.
inline void paint (juce::Graphics& g, juce::Rectangle<float> window)
{
    if (window.isEmpty())
        return;
    const auto& light = key_light::current();
    const auto measures = measuresFor (light);
    const auto landing = key_light::landingOn (window.expanded (measures.ring), light);
    const auto margin = main_frame::marginFor (measures);
    const juce::Graphics::ScopedSaveState saved (g);
    const auto left = static_cast<int> (std::ceil (window.getX() + 2.0f));
    const auto top = static_cast<int> (std::ceil (window.getY() + topShadowOf (window, measures) + 1.0f));
    const auto right = static_cast<int> (std::floor (window.getRight() - 2.0f));
    const auto bottom = static_cast<int> (std::floor (window.getBottom() - 2.0f));
    if (right > left && bottom > top)
        g.excludeClipRegion ({ left, top, right - left, bottom - top });
    material_cache::draw (g, window.expanded (margin),
                          { 3, { landing.fraction, landing.strength, measures.ring, measures.shadow } }, {},
                          [&landing, &measures, margin] (juce::Graphics& target, juce::Rectangle<float> local) {
                              uncached::paint (target, local.reduced (margin), landing, measures); }, {},
                          material_cache::InitialPaint::canonicalRaster);
}
}
