#include "HyphaAttackSpecimenPainter.h"

#include "HyphaAttackMembraneGeometry.h"
#include "HyphaAttackUiContract.h"

namespace hypha::attack_specimen
{
void drawSpecimen (juce::Graphics& g, juce::Rectangle<int> area, FeatureAmounts raw)
{
    using attack_specimen_geometry::unit;
    const FeatureAmounts amounts { unit (raw.strength), unit (raw.texture), unit (raw.sharpness) };
    if (area.getWidth() < 8 || area.getHeight() < 8)
        return;
    const auto geometry = attack_specimen_geometry::geometry (area.toFloat().reduced (2), amounts);
    if (geometry.outline.isEmpty())
        return;

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);
    const auto cyan = juce::Colour (attack_ui::sharpnessColour);
    const auto copper = juce::Colour (attack_ui::textureColour);
    const auto gold = juce::Colour (attack_ui::strengthColour);

    const auto sharpnessAlpha = .05f + amounts.sharpness * .55f;
    g.setColour (cyan.withAlpha (sharpnessAlpha * .30f));
    for (const auto& arc : geometry.sharpnessArcs)
        g.strokePath (arc, juce::PathStrokeType (1.5f + amounts.sharpness * 4.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (cyan.withAlpha (juce::jmin (.75f, sharpnessAlpha * 1.40f)));
    for (const auto& arc : geometry.sharpnessArcs)
        g.strokePath (arc, juce::PathStrokeType (.75f + amounts.sharpness,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::ColourGradient body (
        juce::Colour (0xff101f24).withAlpha (.86f), geometry.bounds.getTopLeft(),
        juce::Colour (0xff061115).withAlpha (.94f), geometry.bounds.getBottomRight(), false);
    body.addColour (.24, juce::Colour (0xff173036).withAlpha (.88f));
    body.addColour (.58, juce::Colour (0xff0b1a1d).withAlpha (.92f));
    g.setGradientFill (body);
    g.fillPath (geometry.outline);

    constexpr std::array<float, 3> tissueAlpha { .09f, .065f, .05f };
    for (std::size_t index = 0; index < geometry.tissue.size(); ++index)
    {
        const auto direction = index == 1 ? geometry.bounds.getBottomLeft()
                                          : geometry.bounds.getTopLeft();
        juce::ColourGradient density (
            copper.darker (.65f).withAlpha (.015f), direction,
            gold.withAlpha (tissueAlpha[index]), geometry.bounds.getCentre(), false);
        density.addColour (.62, copper.withAlpha (tissueAlpha[index] * .55f));
        g.setGradientFill (density);
        g.fillPath (geometry.tissue[index]);
    }

    const auto fibreWidth = .45f + amounts.texture * .75f;
    {
        juce::Graphics::ScopedSaveState fibreClip (g);
        g.reduceClipRegion (geometry.outline);
        const auto fibreAlpha = 1.10f
            / (static_cast<float> (geometry.fibreCount) * fibreWidth);
        g.setColour (copper.withAlpha (juce::jmin (.82f, fibreAlpha)));
        for (std::size_t index = 0; index < geometry.fibreCount; ++index)
            g.strokePath (geometry.fibres[index], juce::PathStrokeType (
                fibreWidth, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        g.setColour (gold.withAlpha (.60f / static_cast<float> (geometry.fibreCount)));
        for (std::size_t index = 0; index < geometry.fibreCount; index += 2)
            g.strokePath (geometry.fibres[index], juce::PathStrokeType (
                .45f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (copper.withAlpha (.20f));
    g.strokePath (geometry.outline, juce::PathStrokeType (.55f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}
