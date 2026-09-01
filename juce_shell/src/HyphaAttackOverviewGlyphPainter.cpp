#include "HyphaAttackOverviewGlyphPainter.h"

#include <cmath>

#include "HyphaAttackUiContract.h"

namespace hypha::attack_overview_glyph
{
namespace
{
const auto strengthColour = juce::Colour (attack_ui::strengthColour);
const auto brightnessColour = juce::Colour (attack_ui::brightnessColour);
const auto transientColour = juce::Colour (attack_ui::transientColour);
const auto textureColour = juce::Colour (attack_ui::textureColour);

float visibleAmount (float amount) noexcept
{
    return std::sqrt (juce::jlimit (0.0f, 1.0f, amount));
}

juce::Path membrane (juce::Rectangle<int> area, float radius, float reach, float bias)
{
    const auto x = static_cast<float> (area.getX());
    const auto y = static_cast<float> (area.getCentreY());
    const auto width = static_cast<float> (area.getWidth());
    const auto height = static_cast<float> (area.getHeight());
    const auto tip = x + width * reach;
    const auto spread = height * radius;
    juce::Path path;
    path.startNewSubPath (x, y + spread * 0.02f);
    path.cubicTo (x + width * 0.10f, y - spread * (0.28f + bias),
                  x + width * 0.18f, y - spread,
                  x + width * 0.34f, y - spread * 0.94f);
    path.cubicTo (x + width * 0.54f, y - spread * 0.70f,
                  tip - width * 0.10f, y - spread * 0.15f, tip, y);
    path.cubicTo (tip - width * 0.12f, y + spread * 0.12f,
                  x + width * 0.55f, y + spread * 0.63f,
                  x + width * 0.33f, y + spread * 0.88f);
    path.cubicTo (x + width * 0.17f, y + spread * (0.82f - bias),
                  x + width * 0.08f, y + spread * 0.22f, x, y + spread * 0.02f);
    path.closeSubPath();
    return path;
}

void fillLayer (juce::Graphics& g, juce::Rectangle<int> area,
                float amount, float opacity, float radius, float reach,
                juce::Colour colour)
{
    const auto visible = visibleAmount (amount);
    if (visible <= 0.0f)
        return;
    constexpr int feathers = 4;
    for (int index = 0; index < feathers; ++index)
    {
        const auto progress = static_cast<float> (index) / static_cast<float> (feathers - 1);
        g.setColour (colour.withAlpha (
            opacity * visible * (0.025f + progress * 0.052f)));
        g.fillPath (membrane (area,
                              radius * visible * (1.12f - progress * 0.20f),
                              reach * (0.94f + progress * 0.06f),
                              (progress - 0.5f) * 0.08f));
    }
}

void strokeMembranes (juce::Graphics& g, juce::Rectangle<int> area,
                      float amount, float opacity, float radius, float reach,
                      juce::Colour colour)
{
    const auto visible = visibleAmount (amount);
    if (visible <= 0.0f)
        return;
    for (int index = 0; index < 4; ++index)
    {
        const auto progress = static_cast<float> (index) / 3.0f;
        g.setColour (colour.withAlpha (opacity * visible * (0.10f + progress * 0.15f)));
        g.strokePath (membrane (area,
                                radius * visible * (0.72f + progress * 0.28f),
                                reach * (0.90f + progress * 0.10f),
                                (progress - 0.55f) * 0.10f),
                      juce::PathStrokeType (index == 3 ? 0.88f : 0.50f,
                                            juce::PathStrokeType::curved));
    }
}

void drawFibres (juce::Graphics& g, juce::Rectangle<int> area,
                 float amount, float opacity, float radius, float reach,
                 juce::Colour colour, int count)
{
    const auto visible = visibleAmount (amount);
    if (visible <= 0.0f)
        return;
    const auto x = static_cast<float> (area.getX());
    const auto y = static_cast<float> (area.getCentreY());
    const auto width = static_cast<float> (area.getWidth());
    const auto height = static_cast<float> (area.getHeight());
    for (int index = 0; index < count; ++index)
    {
        const auto phase = static_cast<float> (index + 1) / static_cast<float> (count + 1);
        const auto offset = (phase - 0.5f) * height * radius * visible;
        const auto curl = std::sin (phase * 3.7f * juce::MathConstants<float>::pi)
                        * height * radius * 0.18f * visible;
        juce::Path fibre;
        fibre.startNewSubPath (x + width * (0.015f + phase * 0.025f), y + offset * 0.12f);
        fibre.cubicTo (x + width * 0.20f, y + offset + curl,
                       x + width * 0.48f, y + offset * 0.58f - curl * 0.45f,
                       x + width * reach, y + offset * 0.08f);
        g.setColour (colour.withAlpha (opacity * visible
            * (index % 3 == 0 ? 0.24f : 0.14f)));
        g.strokePath (fibre, juce::PathStrokeType (index % 3 == 0 ? 0.62f : 0.38f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
}

void drawLayers (juce::Graphics& g, juce::Rectangle<int> area,
                 attack_specimen::FeatureAmounts amounts, float opacity)
{
    strokeMembranes (g, area, amounts.transient, opacity, 0.47f, 1.00f, transientColour);
    strokeMembranes (g, area, amounts.brightness, opacity, 0.40f, 0.86f, brightnessColour);
    fillLayer (g, area, amounts.texture, opacity, 0.31f, 0.74f, textureColour);
    drawFibres (g, area, amounts.texture, opacity, 0.31f, 0.74f, textureColour, 8);
    fillLayer (g, area, amounts.strength, opacity, 0.20f, 0.57f, strengthColour);
    drawFibres (g, area, amounts.strength, opacity, 0.19f, 0.60f, strengthColour, 5);
}
}

void drawAbsolute (juce::Graphics& g, juce::Rectangle<int> area,
                   attack_specimen::FeatureAmounts amounts)
{
    if (area.getWidth() >= 4 && area.getHeight() >= 4)
        drawLayers (g, area, amounts, 1.0f);
}

void drawComparison (juce::Graphics& g, juce::Rectangle<int> area,
                     attack_specimen::FeatureAmounts preAmounts,
                     attack_specimen::FeatureAmounts postAmounts)
{
    if (area.getWidth() < 4 || area.getHeight() < 4)
        return;
    drawLayers (g, area, preAmounts, 0.30f);
    drawLayers (g, area, postAmounts, 0.86f);
    drawLayers (g, area, preAmounts, 0.08f);
}
}
