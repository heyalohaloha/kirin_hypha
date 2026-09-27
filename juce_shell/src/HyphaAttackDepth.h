#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// DRUM depth material (2.5D). One key light from the upper left, a cool rim light from below and
// recessed glass wells give the observation surface physical depth without a GPU. Measured light
// stays sharp: depth belongs to the vessel, never to a value. Every effect is drawn inside the
// region whose data it belongs to, so nothing crosses from HISTORY into a lane, from one hit column
// into another, or from a value's side of the zero line to the other.
namespace hypha::attack_depth
{
struct Look
{
    float wellShadow = 0.0f; // inner shadow along the upper and left walls of a recessed well
    float wellRim = 0.0f;    // cool bounce light along the lower and right rims
    float sheen = 0.0f;      // static glass reflection across the upper part of a well
    float vignette = 0.0f;   // darker ends of the HISTORY well
    float bloom = 0.0f;      // soft outer light around measured strokes
    float specular = 0.0f;   // key-light highlight inside the upper envelope edge, shade below
    float lift = 0.0f;       // shadow that lifts measured light above the glass floor
    float pin = 0.0f;        // left-lit, right-shaded per-hit bars
    float tip = 0.0f;        // lit tip at a bar's value end and its contact light at zero
    float age = 0.0f;        // time depth: measured light fades with age (share lost at -6 s)
    float sphere = 0.0f;     // lit sphere shading for the selected bulb and spores
    float engrave = 0.0f;    // etched baselines and title
    float graticule = 0.0f;  // fine instrument ticks
    float fresnel = 0.0f;    // glass-tube rim: measured bodies glow just inside their edges
    float halo = 0.0f;       // the selected hit lights the HISTORY glass around it
    float rail = 0.0f;       // each lane's zero line becomes a lit rail in the lane colour
    float glint = 0.0f;      // key light catching the crest of each measured peak
};

// The shipped look. Lanes crowded with hits fall back to a Look {} (every effect off).
const Look& look() noexcept;

// Chrome. A recessed glass well: inner shadow, rim light, sheen and (for HISTORY) vignette.
void paintWell (juce::Graphics&, juce::Rectangle<float> area, float radius, bool vignette);

// Glints where the key light catches the crests of the upper edge: local peaks that rise above
// crestY (smaller y is higher). Low bumps between hits stay unlit.
void paintGlints (juce::Graphics&, const juce::Path& edge, float crestY, juce::Colour, float strength,
                  juce::Rectangle<float> plot);

// Chrome. The light lip of an etched line, one pixel below its groove.
void engraveLip (juce::Graphics&, float y, float x0, float x1);

// Soft light around a stroke: stacked wide, faint strokes approximate a small blur.
void strokeBloom (juce::Graphics&, const juce::Path&, juce::Colour, float strength);

// Light along a measured shape: a glass-tube rim that straddles the edge (half inside as the
// tube wall, half outside as its glow) and a key-light highlight just inside the upper edge.
// `upper` is the part of the edge that faces the key light.
void lightVolume (juce::Graphics&, const juce::Path& edge, const juce::Path& upper,
                  juce::Colour colour, float fresnel, float specular, float offset);

// Light from the selected hit onto the HISTORY glass, clipped to the plot.
void paintHalo (juce::Graphics&, juce::Rectangle<int> plot, float x, juce::Colour);

// A lit sphere for the selected bulb and spores. The key light sits upper left.
void paintSphere (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour);

// Time depth: the brightness left to measured light at `x` in a plot whose right end is NOW.
// 1 at NOW, falling steadily to 1 - age at the oldest end.
float ageLight (float x, juce::Rectangle<float> plot) noexcept;

// A horizontal brush for measured light: the colour at `alpha`, fading with age to the left.
juce::ColourGradient ageBrush (juce::Colour, float alpha, juce::Rectangle<float> plot);

// A horizontal brush that darkens toward the left by the light that age has taken away.
juce::ColourGradient ageShade (juce::Colour floor, juce::Rectangle<float> plot);
}
