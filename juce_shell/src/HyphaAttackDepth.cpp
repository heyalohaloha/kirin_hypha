#include "HyphaAttackDepth.h"

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
    0.060f, // haze
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
    if (area.getWidth() < 6.0f || area.getHeight() < 6.0f
        || (depth.wellShadow <= 0.0f && depth.wellRim <= 0.0f && depth.sheen <= 0.0f
            && (! vignette || depth.vignette <= 0.0f)))
        return;
    juce::Graphics::ScopedSaveState saved (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, radius);
    g.reduceClipRegion (clip);
    const auto black = juce::Colours::black;
    if (depth.wellShadow > 0.0f)
    {
        // The upper and left walls face away from the key light.
        const auto drop = juce::jlimit (3.0f, 12.0f, area.getHeight() * 0.16f);
        g.setGradientFill ({ black.withAlpha (depth.wellShadow), 0.0f, area.getY(),
                             black.withAlpha (0.0f), 0.0f, area.getY() + drop, false });
        g.fillRect (area.withHeight (drop));
        const auto side = juce::jlimit (2.0f, 8.0f, area.getWidth() * 0.02f);
        g.setGradientFill ({ black.withAlpha (depth.wellShadow * 0.6f), area.getX(), 0.0f,
                             black.withAlpha (0.0f), area.getX() + side, 0.0f, false });
        g.fillRect (area.withWidth (side));
    }
    if (vignette && depth.vignette > 0.0f)
    {
        const auto reach = area.getWidth() * 0.12f;
        g.setGradientFill ({ black.withAlpha (0.0f), area.getRight() - reach, 0.0f,
                             black.withAlpha (depth.vignette * 0.6f), area.getRight(), 0.0f, false });
        g.fillRect (area.withLeft (area.getRight() - reach));
        const auto low = area.getHeight() * 0.24f;
        g.setGradientFill ({ black.withAlpha (0.0f), 0.0f, area.getBottom() - low,
                             black.withAlpha (depth.vignette * 0.5f), 0.0f, area.getBottom(), false });
        g.fillRect (area.withTop (area.getBottom() - low));
    }
    if (vignette && depth.haze > 0.0f)
    {
        // A little of the measured light hangs in the glass around the middle of the well.
        const auto haze = juce::Colour (attack_ui::waveformColour);
        const auto centre = area.getCentre();
        g.setGradientFill ({ haze.withAlpha (depth.haze), centre.x, centre.y,
                             haze.withAlpha (0.0f), centre.x, area.getY() - area.getHeight() * 0.1f,
                             true });
        g.fillRect (area);
    }
    if (depth.sheen > 0.0f)
    {
        // A still reflection on the glass cover, falling away before the centre of the well.
        juce::Path band;
        band.startNewSubPath (area.getX(), area.getY());
        band.lineTo (area.getX() + area.getWidth() * 0.62f, area.getY());
        band.lineTo (area.getX() + area.getWidth() * 0.34f, area.getBottom());
        band.lineTo (area.getX(), area.getBottom());
        band.closeSubPath();
        g.setGradientFill ({ COL_NORMAL.withAlpha (depth.sheen), area.getX(), area.getY(),
                             COL_NORMAL.withAlpha (0.0f), area.getX() + area.getWidth() * 0.36f,
                             area.getBottom(), false });
        g.fillPath (band);
    }
    if (depth.wellRim > 0.0f)
    {
        // The lower and right walls catch the light and bounce a little of it back up.
        const auto rim = juce::Colour (attack_ui::waveformColour);
        const auto bounce = juce::jlimit (2.0f, 8.0f, area.getHeight() * 0.08f);
        g.setGradientFill ({ rim.withAlpha (0.0f), 0.0f, area.getBottom() - bounce,
                             rim.withAlpha (depth.wellRim * 0.35f), 0.0f, area.getBottom(), false });
        g.fillRect (area.withTop (area.getBottom() - bounce));
        g.setColour (rim.withAlpha (depth.wellRim));
        g.fillRect (juce::Rectangle<float> (area.getX() + radius, area.getBottom() - 1.0f,
                                            area.getWidth() - 2.0f * radius, 1.0f));
        // The cut glass edge: a fine ivory lip just above the lit lower rim.
        g.setColour (COL_NORMAL.withAlpha (depth.wellRim * 0.55f));
        g.fillRect (juce::Rectangle<float> (area.getX() + radius * 2.0f, area.getBottom() - 2.0f,
                                            area.getWidth() - 4.0f * radius, 0.6f));
        g.setColour (rim.withAlpha (depth.wellRim * 0.45f));
        g.fillRect (juce::Rectangle<float> (area.getRight() - 1.0f, area.getY() + radius,
                                            1.0f, area.getHeight() - 2.0f * radius));
    }
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
        const auto strength = light * ageLight (at.x, plot);
        g.setColour (glow.withAlpha (juce::jmin (1.0f, 0.16f * strength)));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (at));
        g.setColour (glow.withAlpha (juce::jmin (1.0f, 0.38f * strength)));
        g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (at));
        g.setColour (COL_NORMAL.withAlpha (juce::jmin (1.0f, 0.85f * strength)));
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
