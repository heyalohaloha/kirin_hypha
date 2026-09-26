#include "HyphaAttackDepth.h"
#include "HyphaDepthMaterial.h"

#include <cmath>

#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha::attack_depth
{
namespace
{
// The approved depth (A3, 2026-09-26): the richest of the three reviewed strengths.
constexpr Look product {
    0.58f,  // wellShadow
    0.22f,  // wellRim
    0.045f, // sheen
    0.35f,  // vignette
    2.20f,  // bloom
    0.50f,  // specular
    0.60f,  // lift
    1.00f,  // pin
    1.00f,  // tip
    0.72f,  // age
    1.00f,  // sphere
    0.90f,  // engrave
    0.24f,  // graticule
    0.32f,  // fresnel
    0.090f, // halo
    0.65f,  // rail
    0.90f,  // glint
};
}

const Look& look() noexcept
{
    return product;
}

void paintWell (juce::Graphics& g, juce::Rectangle<float> area, float radius, bool vignette)
{
    const auto& depth = look();
    const auto gold = juce::Colour (attack_ui::waveformColour);
    depth_material::paintRecessedWell (g, area, radius,
        { depth.wellShadow, depth.wellRim, depth.sheen, vignette ? depth.vignette : 0.0f, 0.0f,
          gold });
}

void paintGlints (juce::Graphics& g, const juce::Path& edge, float crestY, juce::Colour colour,
                  float strength, juce::Rectangle<float> plot)
{
    if (strength <= 0.0f || edge.isEmpty())
        return;
    const auto light = strength;
    // Crests are the upper edge's local peaks. The envelope keeps every extremum when it is
    // simplified, so each measured crest is a vertex here.
    const auto glow = colour.interpolatedWith (COL_NORMAL, 0.35f);
    juce::Path::Iterator it (edge);
    juce::Point<float> previous, current;
    int count = 0;
    bool falling = false;
    const auto glintAt = [&] (juce::Point<float> at) {
        const auto lit = light * ageLight (at.x, plot);
        g.setColour (glow.withAlpha (juce::jmin (1.0f, 0.16f * lit)));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (at));
        g.setColour (glow.withAlpha (juce::jmin (1.0f, 0.38f * lit)));
        g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (at));
        g.setColour (COL_NORMAL.withAlpha (juce::jmin (1.0f, 0.85f * lit)));
        g.fillEllipse (juce::Rectangle<float> (1.6f, 1.6f).withCentre (at));
    };
    while (it.next())
    {
        if (it.elementType == juce::Path::Iterator::startNewSubPath)
        {
            current = { it.x1, it.y1 };
            count = 1;
            falling = false;
            continue;
        }
        if (it.elementType != juce::Path::Iterator::lineTo)
            continue;
        const juce::Point<float> next { it.x1, it.y1 };
        if (count >= 2 && current.y < crestY && current.y < previous.y - 0.5f
            && next.y > current.y + 0.5f && ! falling)
            glintAt (current);
        falling = next.y > current.y;
        previous = current;
        current = next;
        ++count;
    }
}

void engraveLip (juce::Graphics& g, float y, float x0, float x1)
{
    const auto engrave = look().engrave;
    if (engrave <= 0.0f || ! (x1 > x0))
        return;
    const auto row = juce::roundToInt (y);
    g.setColour (juce::Colours::black.withAlpha (0.50f * engrave));
    g.drawHorizontalLine (row - 1, x0, x1);
    g.setColour (COL_NORMAL.withAlpha (0.07f * engrave));
    g.drawHorizontalLine (row + 1, x0, x1);
}

void strokeBloom (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float strength)
{
    if (strength <= 0.0f || path.isEmpty())
        return;
    g.setColour (colour.withAlpha (juce::jmin (1.0f, 0.065f * strength)));
    g.strokePath (path, juce::PathStrokeType (5.0f, juce::PathStrokeType::beveled));
}

void lightVolume (juce::Graphics& g, const juce::Path& edge, const juce::Path& upper,
                  juce::Colour colour, float fresnel, float specular, float offset)
{
    if (fresnel > 0.0f && ! edge.isEmpty())
    {
        // A transparent tube is brightest where the eye looks through the most glass: its walls.
        g.setColour (colour.withAlpha (juce::jmin (1.0f, fresnel)));
        g.strokePath (edge, juce::PathStrokeType (5.5f, juce::PathStrokeType::beveled));
    }
    if (specular > 0.0f && ! upper.isEmpty())
    {
        g.setColour (colour.interpolatedWith (COL_NORMAL, 0.55f).withAlpha (juce::jmin (1.0f, specular)));
        g.strokePath (upper, juce::PathStrokeType (1.1f),
                      juce::AffineTransform::translation (0.0f, offset));
    }
}

void paintHalo (juce::Graphics& g, juce::Rectangle<int> plot, float x, juce::Colour colour)
{
    const auto halo = look().halo;
    if (halo <= 0.0f || plot.isEmpty() || ! std::isfinite (x))
        return;
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (plot);
    const auto reach = juce::jlimit (16.0f, 64.0f, static_cast<float> (plot.getWidth()) * 0.07f);
    const auto area = plot.toFloat();
    g.setGradientFill ({ colour.withAlpha (0.0f), x - reach, 0.0f, colour.withAlpha (halo), x, 0.0f,
                         false });
    g.fillRect (area.withLeft (x - reach).withRight (x));
    g.setGradientFill ({ colour.withAlpha (halo), x, 0.0f, colour.withAlpha (0.0f), x + reach, 0.0f,
                         false });
    g.fillRect (area.withLeft (x).withRight (x + reach));
}

void paintSphere (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour)
{
    // The body turns from the pure colour toward its shadow; only a one-pixel specular point is
    // lighter, so a gold sphere never takes on the pale selection colour.
    const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setGradientFill ({ colour, centre.x - radius * 0.35f, centre.y - radius * 0.40f,
                         colour.darker (0.75f), centre.x + radius * 0.85f,
                         centre.y + radius * 0.95f, true });
    g.fillEllipse (body);
    g.setColour (COL_NORMAL.withAlpha (0.90f));
    g.fillEllipse (juce::Rectangle<float> (1.3f, 1.3f).withCentre (
        { centre.x - radius * 0.40f, centre.y - radius * 0.45f }));
}

float ageLight (float x, juce::Rectangle<float> plot) noexcept
{
    const auto age = look().age;
    if (age <= 0.0f || plot.getWidth() <= 0.0f)
        return 1.0f;
    const auto t = juce::jlimit (0.0f, 1.0f, (plot.getRight() - x) / plot.getWidth()); // 0 = NOW
    return 1.0f - age * t; // steadily fainter toward the oldest end
}

juce::ColourGradient ageShade (juce::Colour floor, juce::Rectangle<float> plot)
{
    juce::ColourGradient shade (floor.withAlpha (1.0f - ageLight (plot.getX(), plot)), plot.getX(), 0.0f,
                                floor.withAlpha (0.0f), plot.getRight(), 0.0f, false);
    for (const auto at : { 0.25f, 0.5f, 0.75f })
        shade.addColour (at, floor.withAlpha (1.0f - ageLight (plot.getX() + plot.getWidth() * at, plot)));
    return shade;
}

juce::ColourGradient ageBrush (juce::Colour colour, float alpha, juce::Rectangle<float> plot)
{
    juce::ColourGradient brush (colour.withAlpha (alpha * ageLight (plot.getX(), plot)), plot.getX(), 0.0f,
                                colour.withAlpha (alpha), plot.getRight(), 0.0f, false);
    for (const auto at : { 0.25f, 0.5f, 0.75f })
        brush.addColour (at, colour.withAlpha (alpha * ageLight (plot.getX() + plot.getWidth() * at, plot)));
    return brush;
}
}
