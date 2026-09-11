#include "HyphaJungleMaterial.h"

#include "HyphaTheme.h"

namespace hypha::jungle_material
{
namespace
{
float roleStrength (observatory::Role role) noexcept
{
    return role == observatory::Role::pre ? 0.72f : 1.0f;
}

juce::Path lowerMembrane (juce::Rectangle<float> area, float lift, float phase)
{
    const auto y = area.getBottom() - area.getHeight() * lift;
    juce::Path path;
    path.startNewSubPath (area.getX(), area.getBottom());
    path.lineTo (area.getX(), y);
    path.cubicTo (area.getX() + area.getWidth() * 0.20f,
                  y - area.getHeight() * (0.08f + phase),
                  area.getX() + area.getWidth() * 0.34f,
                  y + area.getHeight() * (0.05f - phase),
                  area.getCentreX(), y - area.getHeight() * 0.015f);
    path.cubicTo (area.getX() + area.getWidth() * 0.67f,
                  y - area.getHeight() * (0.09f - phase),
                  area.getX() + area.getWidth() * 0.81f,
                  y + area.getHeight() * (0.045f + phase),
                  area.getRight(), y - area.getHeight() * 0.035f);
    path.lineTo (area.getRight(), area.getBottom());
    path.closeSubPath();
    return path;
}

void paintContinuousRoot (juce::Graphics& g, juce::Rectangle<float> area,
                          float height, float bend, juce::Colour colour, float thickness)
{
    const auto y = area.getBottom() - area.getHeight() * height;
    juce::Path root;
    root.startNewSubPath (area.getX(), y);
    root.cubicTo (area.getX() + area.getWidth() * 0.23f,
                  y - area.getHeight() * bend,
                  area.getX() + area.getWidth() * 0.64f,
                  y + area.getHeight() * bend * 0.72f,
                  area.getRight(), y - area.getHeight() * bend * 0.42f);
    g.setColour (colour);
    g.strokePath (root, juce::PathStrokeType (
        thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}

void paintInstrumentAcceleration (juce::Graphics& g,
                                  juce::Rectangle<float> area,
                                  observatory::Role role,
                                  observatory::Density density,
                                  bool capture)
{
    if (area.isEmpty() || density == observatory::Density::compact)
        return;
    const auto strength = roleStrength (role) * (capture ? 1.12f : 1.0f);
    const auto inner = area.reduced (2.5f);

    g.setColour (COL_FLORA_BR.withAlpha (0.14f * strength));
    g.drawRoundedRectangle (inner, capture ? 5.0f : 3.8f, capture ? 0.9f : 0.65f);
    g.setColour (COL_SPECTRUM_POST.withAlpha (0.085f * strength));
    g.drawRoundedRectangle (inner.reduced (1.2f), capture ? 4.2f : 3.0f, 0.6f);

    if (! observatory::isFullDensity (density))
        return;

    const auto band = inner.withY (inner.getBottom() - inner.getHeight() * 0.12f)
                           .withHeight (inner.getHeight() * 0.12f);
    const auto membrane = lowerMembrane (band, 0.28f, 0.025f);
    juce::ColourGradient tissue (
        COL_SPECTRUM_POST.withAlpha (0.0f), band.getX(), band.getCentreY(),
        COL_FLORA.withAlpha (0.14f * strength), band.getRight(), band.getCentreY(), false);
    tissue.addColour (0.48, COL_SPECTRUM_POST.withAlpha (0.11f * strength));
    g.setGradientFill (tissue);
    g.fillPath (membrane);
    paintContinuousRoot (g, band, 0.35f, 0.18f,
                         COL_FLORA_BR.withAlpha (0.21f * strength), 0.75f);
}

void paintApertureAcceleration (juce::Graphics& g,
                                juce::Rectangle<float> aperture,
                                observatory::Role role)
{
    if (aperture.isEmpty())
        return;
    const auto strength = roleStrength (role);
    juce::ColourGradient tissue (
        COL_SPECTRUM_POST.withAlpha (0.19f * strength),
        aperture.getX(), aperture.getY(),
        COL_FLORA_BR.withAlpha (0.12f * strength),
        aperture.getRight(), aperture.getBottom(), false);
    tissue.addColour (0.55, BG.withAlpha (0.02f));
    g.setGradientFill (tissue);
    g.fillEllipse (aperture.expanded (1.4f));
    g.setColour (COL_FLORA_BR.withAlpha (0.42f * strength));
    g.drawEllipse (aperture.reduced (0.7f), 0.75f);
    g.setColour (COL_SPECTRUM_POST.withAlpha (0.30f * strength));
    g.drawEllipse (aperture.reduced (2.1f), 0.65f);
}

void paintVuAcceleration (juce::Graphics& g,
                          juce::Rectangle<float> bounds,
                          observatory::Role role)
{
    if (bounds.isEmpty())
        return;
    const auto strength = roleStrength (role);
    auto face = juce::Rectangle<float> (
        bounds.getX() + bounds.getWidth() * 0.018f,
        bounds.getY() + bounds.getHeight() * 0.135f,
        bounds.getWidth() * 0.964f, bounds.getHeight() * 0.565f).reduced (2.0f);

    juce::Graphics::ScopedSaveState saved (g);
    juce::Path glassClip;
    glassClip.addRoundedRectangle (face, face.getHeight() * 0.12f);
    g.reduceClipRegion (glassClip);

    paintContinuousRoot (g, face, 0.115f, 0.045f,
                         COL_FLORA_BR.withAlpha (0.28f * strength),
                         juce::jmax (0.65f, bounds.getWidth() / 1'100.0f));
    paintContinuousRoot (g, face, 0.185f, -0.052f,
                         COL_SPECTRUM_POST.withAlpha (0.24f * strength),
                         juce::jmax (0.60f, bounds.getWidth() / 1'250.0f));

    juce::Path branch;
    branch.startNewSubPath (face.getX() + face.getWidth() * 0.08f,
                            face.getBottom() - face.getHeight() * 0.10f);
    branch.cubicTo (face.getX() + face.getWidth() * 0.18f,
                    face.getBottom() - face.getHeight() * 0.22f,
                    face.getX() + face.getWidth() * 0.31f,
                    face.getBottom() - face.getHeight() * 0.05f,
                    face.getX() + face.getWidth() * 0.43f,
                    face.getBottom() - face.getHeight() * 0.16f);
    juce::ColourGradient rootLight (
        COL_SPECTRUM_POST.withAlpha (0.05f * strength), branch.getBounds().getX(), 0.0f,
        COL_FLORA_BR.withAlpha (0.32f * strength), branch.getBounds().getRight(), 0.0f,
        false);
    g.setGradientFill (rootLight);
    g.strokePath (branch, juce::PathStrokeType (
        juce::jmax (0.55f, bounds.getWidth() / 1'350.0f),
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (COL_FLORA_BR.withAlpha (0.18f * strength));
    g.drawRoundedRectangle (face.reduced (1.4f), face.getHeight() * 0.11f,
                            juce::jmax (0.65f, bounds.getWidth() / 1'200.0f));
}
}
