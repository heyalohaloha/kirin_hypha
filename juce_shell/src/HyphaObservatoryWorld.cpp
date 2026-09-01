#include "HyphaObservatoryWorld.h"

#include <BinaryData.h>

#include "HyphaTheme.h"

#include <array>

namespace hypha::observatory_world
{
namespace
{
const juce::Image& sharedBackdropImage()
{
    static const juce::Image shared = juce::ImageFileFormat::loadFrom (
        BinaryData::observatory_understory_png,
        static_cast<size_t> (BinaryData::observatory_understory_pngSize));
    return shared;
}

const juce::Image& sharedHyphaSpecimenImage()
{
    static const juce::Image shared = juce::ImageFileFormat::loadFrom (
        BinaryData::bg_mycelium_png,
        static_cast<size_t> (BinaryData::bg_mycelium_pngSize));
    return shared;
}

const std::array<juce::Image, 4>& sharedHyphaSpecimenVariants()
{
    static const std::array<juce::Image, 4> variants = []
    {
        const auto& source = sharedHyphaSpecimenImage();
        return std::array<juce::Image, 4> {
            source.rescaled (174, 116, juce::Graphics::mediumResamplingQuality),
            source.rescaled (240, 160, juce::Graphics::mediumResamplingQuality),
            source.rescaled (270, 180, juce::Graphics::mediumResamplingQuality),
            source,
        };
    }();
    return variants;
}

size_t densityIndex (observatory::Density density) noexcept
{
    return density == observatory::Density::compact ? 0u
         : density == observatory::Density::focused ? 1u
         : density == observatory::Density::standard ? 2u : 3u;
}

float visualScale (const State& state) noexcept
{
    return state.density == observatory::Density::compact ? 0.62f
         : state.density == observatory::Density::focused ? 0.78f
         : state.density == observatory::Density::standard ? 0.90f : 1.0f;
}

void paintTimeStrata (juce::Graphics& g, juce::Rectangle<float> area, const State& state)
{
    const float scale = visualScale (state);
    const float depth = area.getHeight() * (0.20f + 0.18f * state.energy);
    for (int layer = 0; layer < 5; ++layer)
    {
        const float proportion = static_cast<float> (layer) / 4.0f;
        const float y = area.getBottom() - depth * proportion;
        juce::Path stratum;
        stratum.startNewSubPath (area.getX(), y);
        stratum.cubicTo (area.getX() + area.getWidth() * (0.24f + state.direction * 0.03f),
                         y - depth * 0.10f,
                         area.getX() + area.getWidth() * (0.66f + state.direction * 0.04f),
                         y + depth * 0.08f,
                         area.getRight(), y - depth * 0.04f);
        g.setColour ((layer % 2 == 0 ? COL_FLORA : COL_SPECTRUM_POST)
                         .withAlpha ((0.028f + state.energy * 0.030f) * scale));
        g.strokePath (stratum, juce::PathStrokeType (0.55f + proportion * 0.35f));
    }
}

void paintFrequencyRoots (juce::Graphics& g, juce::Rectangle<float> area, const State& state)
{
    const float scale = visualScale (state);
    for (int route = 0; route < 6; ++route)
    {
        const float lane = static_cast<float> (route + 1) / 7.0f;
        const float startY = area.getBottom() - area.getHeight() * lane * 0.55f;
        const float endY = area.getY() + area.getHeight() * (0.14f + lane * 0.62f);
        juce::Path root;
        root.startNewSubPath (area.getX(), startY);
        root.cubicTo (area.getX() + area.getWidth() * 0.24f,
                      startY - area.getHeight() * (0.04f + lane * 0.03f),
                      area.getX() + area.getWidth() * 0.69f,
                      endY + area.getHeight() * (0.05f - lane * 0.02f),
                      area.getRight(), endY);
        g.setColour ((route % 2 == 0 ? COL_SPECTRUM_PRE : COL_FLORA)
                         .withAlpha ((0.026f + (state.active ? 0.018f : 0.0f)) * scale));
        g.strokePath (root, juce::PathStrokeType (0.55f + 0.12f * route));
        if (state.density == observatory::Density::observatory && route > 0 && route < 5)
        {
            const float nodeX = area.getX() + area.getWidth() * (0.18f + 0.15f * route);
            const float nodeY = startY + (endY - startY) * (0.22f + 0.10f * route);
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.08f + state.energy * 0.05f));
            g.fillEllipse (nodeX - 1.4f, nodeY - 1.4f, 2.8f, 2.8f);
        }
    }
}

void paintSpaceMembrane (juce::Graphics& g, juce::Rectangle<float> area, const State& state)
{
    if (! state.active)
        return;
    auto membrane = area.reduced (area.getWidth() * 0.17f, area.getHeight() * 0.12f);
    for (int layer = 0; layer < 4; ++layer)
    {
        const float inset = static_cast<float> (layer) * membrane.getHeight() * 0.075f;
        const auto field = membrane.reduced (inset, inset * 0.60f);
        juce::Path shell;
        shell.startNewSubPath (field.getX(), field.getCentreY());
        shell.cubicTo (field.getX() + field.getWidth() * 0.23f, field.getY(),
                       field.getX() + field.getWidth() * 0.77f, field.getY(),
                       field.getRight(), field.getCentreY());
        shell.cubicTo (field.getX() + field.getWidth() * 0.77f, field.getBottom(),
                       field.getX() + field.getWidth() * 0.23f, field.getBottom(),
                       field.getX(), field.getCentreY());
        g.setColour ((layer % 2 == 0 ? COL_SPECTRUM_POST : COL_FLORA)
                         .withAlpha (0.025f + state.energy * 0.025f));
        g.strokePath (shell, juce::PathStrokeType (0.7f + layer * 0.18f));
    }
}
}

Backdrop::Backdrop()
{
    image = sharedBackdropImage();
    hyphaSpecimen = sharedHyphaSpecimenImage();
}

void Backdrop::draw (juce::Graphics& g, juce::Rectangle<int> area, const State& state) const
{
    g.setColour (BG);
    g.fillRect (area);
    if (! image.isValid())
        return;

    juce::Graphics::ScopedSaveState saved (g);
    g.setOpacity (juce::jlimit (0.0f, 1.0f, backdropOpacity (state)));
    g.drawImage (image, area.toFloat(), juce::RectanglePlacement::stretchToFit);
}

void Backdrop::drawHyphaSpecimen (juce::Graphics& g,
                                  juce::Rectangle<int> area,
                                  const State& state) const
{
    if (state.domain != observatory::Domain::time || ! hyphaSpecimen.isValid()
        || area.isEmpty())
        return;

    const auto& specimen = sharedHyphaSpecimenVariants()[densityIndex (state.density)];
    const int x = area.getCentreX() - specimen.getWidth() / 2;
    const int y = area.getBottom() - specimen.getHeight();
    juce::Graphics::ScopedSaveState saved (g);
    const float roleOpacity = state.role == observatory::Role::pre ? 0.72f : 1.0f;
    g.setOpacity ((state.active ? 0.92f : 0.52f) * roleOpacity);
    g.drawImageAt (specimen, x, y, false);
}

void paintDomainBed (juce::Graphics& g, juce::Rectangle<int> area, const State& state)
{
    if (area.isEmpty())
        return;
    const auto field = area.reduced (2).toFloat();
    if (state.domain == observatory::Domain::time)
        paintTimeStrata (g, field, state);
    else if (state.domain == observatory::Domain::frequency)
        paintFrequencyRoots (g, field, state);
    else if (state.domain == observatory::Domain::space)
        paintSpaceMembrane (g, field, state);
}

void paintPlateFrame (juce::Graphics& g, juce::Rectangle<int> area, const State& state)
{
    const auto outer = area.toFloat().reduced (1.0f);
    g.setColour (COL_MUTED.withAlpha (state.capture ? 0.68f : 0.44f));
    g.drawRoundedRectangle (outer, state.capture ? 7.0f : 5.0f, state.capture ? 1.2f : 0.8f);
    g.setColour (COL_SPECTRUM_POST.withAlpha (state.capture ? 0.18f : 0.08f));
    g.drawRoundedRectangle (outer.reduced (2.0f), state.capture ? 6.0f : 4.0f, 0.55f);

    if (! state.capture)
        return;
    const float tick = juce::jlimit (8.0f, 18.0f, outer.getWidth() * 0.025f);
    for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
        const bool left = cornerIndex % 2 == 0;
        const bool top = cornerIndex < 2;
        const juce::Point<float> corner {
            left ? outer.getX() : outer.getRight(),
            top ? outer.getY() : outer.getBottom()
        };
        const float xDirection = left ? 1.0f : -1.0f;
        const float yDirection = top ? 1.0f : -1.0f;
        g.setColour (COL_FLORA.withAlpha (0.52f));
        g.drawLine (corner.x, corner.y, corner.x + xDirection * tick, corner.y, 1.0f);
        g.drawLine (corner.x, corner.y, corner.x, corner.y + yDirection * tick, 1.0f);
    }
}

void paintPairRoot (juce::Graphics& g, juce::Rectangle<int> area, const State& state,
                    juce::Colour connectionColour)
{
    if (! state.paired || area.isEmpty())
        return;
    const auto bounds = area.toFloat().reduced (2.0f);
    juce::Path root;
    root.startNewSubPath (bounds.getX(), bounds.getCentreY());
    root.cubicTo (bounds.getX() + bounds.getWidth() * 0.25f, bounds.getY(),
                  bounds.getX() + bounds.getWidth() * 0.62f, bounds.getBottom(),
                  bounds.getRight() - 4.0f, bounds.getCentreY());
    g.setColour (connectionColour.withAlpha (0.20f));
    g.strokePath (root, juce::PathStrokeType (0.8f));
    g.setColour (connectionColour.withAlpha (0.30f));
    g.fillEllipse (bounds.getX() - 1.4f, bounds.getCentreY() - 1.4f, 2.8f, 2.8f);
}

void paintGuideRoot (juce::Graphics& g, juce::Rectangle<int> area, const State& state)
{
    if (! state.guidePresent || area.isEmpty())
        return;
    const auto y = static_cast<float> (area.getBottom()) - 2.0f;
    juce::Path root;
    root.startNewSubPath (static_cast<float> (area.getX()) + 3.0f, y);
    root.cubicTo (area.getX() + area.getWidth() * 0.26f, y - 2.0f,
                  area.getX() + area.getWidth() * 0.72f, y + 1.0f,
                  static_cast<float> (area.getRight()) - 3.0f, y - 1.0f);
    g.setColour (COL_GUIDE.withAlpha (0.22f));
    g.strokePath (root, juce::PathStrokeType (0.65f));
}
}
