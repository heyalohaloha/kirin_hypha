#pragma once

#include <cmath>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaKeyLight.h"
#include "HyphaMaterialCache.h"

// The frame of a page's main window (2026-10-01, stage 2 of the light): the VU chassis's bronze
// bevel around the window's glass, lit by the editor's one key light (HyphaKeyLight.h). The frame
// stands outside the window, so it never covers what the window shows. Only the main window of a
// page has it; cards, panels and lanes stay quiet (HyphaDepthMaterial.h).
namespace hypha::main_frame
{
struct Measures
{
    float ring = 7.0f;   // the bronze bevel around the glass
    float shadow = 6.0f; // the soft shadow the frame casts on the chassis
    float radius = 2.0f; // the glass corner; the frame's outer corner adds the ring
};

// As on the VU chassis at 900 x 600 (a 7 px ring), and never thinner than a bevel can show. A
// magnified editor scales the 900 x 600 layout, so the frame scales with it.
inline Measures measuresFor (const key_light::Light& light) noexcept
{
    const auto unit = light.diagonal / std::hypot (900.0f, 600.0f);
    return { juce::jmax (3.0f, 7.0f * unit), juce::jmax (3.0f, 6.0f * unit), 2.0f };
}

// What the frame and its shadow cover around a window: the shadow's widest stroke, shifted down,
// in whole points so the cached image lands on the same pixels as a direct paint.
inline float marginFor (const Measures& measures) noexcept
{
    return std::ceil (measures.ring + 1.5f * measures.shadow + 3.0f);
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
    g.setGradientFill ({ juce::Colour (0xff9a7440).withAlpha (0.75f * k), lit.x, lit.y,
                         juce::Colour (0xff9a7440).withAlpha (0.0f), lit.x + 0.55f * reach, lit.y, true });
    g.fillPath (bevel);
    g.setColour (black.withAlpha (0.85f));
    g.drawRoundedRectangle (outer.reduced (0.5f), outerRadius, 1.2f);
    // The inner lip where the glass begins: bright where the light lands, dim far from it.
    g.setGradientFill ({ gold.withAlpha (0.95f * k), lit.x, lit.y,
                         gold.withAlpha (0.08f * k), lit.x + reach, lit.y, true });
    g.drawRoundedRectangle (window.expanded (0.6f), measures.radius + 0.6f, 1.4f);
    // A little warm light bounced up onto the lower bevel, under where the light lands.
    juce::ColourGradient bounce (gold.withAlpha (0.0f), window.getX(), 0.0f,
                                 gold.withAlpha (0.0f), window.getRight(), 0.0f, false);
    bounce.addColour (juce::jlimit (0.1, 0.9, static_cast<double> (landing.fraction)), gold.withAlpha (0.28f * k));
    g.setGradientFill (bounce);
    g.fillRect (juce::Rectangle<float> (window.getX() + outerRadius, window.getBottom() + 0.6f,
                                        window.getWidth() - 2.0f * outerRadius, 1.2f));
    // The glass lies below the frame: its upper wall in shadow just inside.
    const auto drop = juce::jmin (3.0f * measures.ring, window.getHeight() * 0.12f);
    g.setGradientFill ({ black.withAlpha (0.38f), 0.0f, window.getY(),
                         black.withAlpha (0.0f), 0.0f, window.getY() + drop, false });
    g.fillRect (window.withHeight (drop));
}
}

// The ring a main window gives up to its frame where the frame has no room outside it: while
// laying out for `context`, or while painting.
inline int insetFor (const presentation::Context& context) noexcept
{
    return juce::roundToInt (measuresFor (key_light::inEditor (context)).ring) + 1;
}

inline int inset() noexcept
{
    return juce::roundToInt (measuresFor (key_light::current()).ring) + 1;
}

// The frame around `window`, lit by the light of the component now painting.
inline void paint (juce::Graphics& g, juce::Rectangle<float> window)
{
    if (window.isEmpty())
        return;
    const auto& light = key_light::current();
    const auto measures = measuresFor (light);
    const auto landing = key_light::landingOn (window.expanded (measures.ring), light);
    const auto margin = marginFor (measures);
    material_cache::draw (g, window.expanded (margin),
                          { 3, { landing.fraction, landing.strength, measures.ring, measures.shadow } }, {},
                          [&landing, &measures, margin] (juce::Graphics& target, juce::Rectangle<float> local) {
                              uncached::paint (target, local.reduced (margin), landing, measures); });
}
}
