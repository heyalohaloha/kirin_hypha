#include "HyphaAttackSpecimenPainter.h"
#include "HyphaAttackMembraneGeometry.h"
#include "HyphaAttackUiContract.h"

namespace hypha::attack_specimen
{
void drawMembrane (juce::Graphics& g, juce::Rectangle<int> area, FeatureAmounts raw,
                   const attack_motion::Motion& motion, bool reference)
{
    using attack_motion::unit;
    const FeatureAmounts a { unit (raw.strength), unit (raw.brightness),
                              unit (raw.transient), unit (raw.texture) };
    if (area.getWidth() < 4 || area.getHeight() < 4
        || (a.strength <= 0 && a.brightness <= 0 && a.transient <= 0 && a.texture <= 0)) return;
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);
    const auto shape = attack_membrane::geometry (area.toFloat().reduced (1), a, motion);
    const auto colour = [reference] (std::uint32_t rgb) {
        return reference ? juce::Colour (0xffa3b3b9) : juce::Colour (rgb); };
    const auto alphaScale = reference ? .32f : 1.0f;
    const auto light = [&] (const attack_membrane::Sheet& sheet, juce::Colour tint, float alpha)
    {
        juce::ColourGradient gradient (tint.withAlpha (0.0f), sheet.lightStart,
                                       tint.withAlpha (0.0f), sheet.lightEnd, false);
        gradient.addColour (.23, tint.darker (.35f).withAlpha (alpha * .42f));
        gradient.addColour (.49, tint.withAlpha (alpha * .82f));
        gradient.addColour (.59, tint.darker (.3f).withAlpha (alpha * .30f));
        gradient.addColour (.82, tint.withAlpha (alpha * .26f));
        g.setGradientFill (gradient);
    };
    const auto longitudinal = [&] (juce::Colour tint, float alpha)
    {
        juce::ColourGradient gradient (tint.withAlpha (0.0f), area.toFloat().getBottomLeft(),
            tint.withAlpha (alpha * .45f), area.toFloat().getTopRight(), false);
        gradient.addColour (.38, tint.withAlpha (alpha * .55f));
        gradient.addColour (.68, tint.withAlpha (alpha));
        g.setGradientFill (gradient);
    };
    const std::array<float, 4> amounts { a.brightness, a.strength, a.brightness, a.texture };
    const std::array<std::uint32_t, 4> tints { attack_ui::brightnessColour, attack_ui::strengthColour,
                                             attack_ui::brightnessColour, attack_ui::textureColour };
    for (std::size_t i = 0; i < shape.sheets.size(); ++i)
    {
        if (amounts[i] <= 0) continue;
        const auto& sheet = shape.sheets[i];
        const auto tint = colour (tints[i]);
        const auto alpha = amounts[i] * alphaScale;
        light (sheet, tint, alpha); g.fillPath (sheet.fills[0]);
        longitudinal (tint, alpha * .15f); g.fillPath (sheet.fills[1]);
        light (sheet, tint, alpha * .44f); g.fillPath (sheet.fills[2]);
        longitudinal (tint, alpha * .49f); g.fillPath (sheet.fills[3]);
        longitudinal (tint, alpha * .15f); g.fillPath (sheet.fills[4]);
    }
    if (a.transient > 0)
    {
        const auto tint = colour (attack_ui::transientColour);
        const auto front = shape.front.getBounds();
        juce::ColourGradient gradient (tint.withAlpha (0.0f), front.getTopLeft(),
            tint.withAlpha (a.transient * alphaScale * .70f), front.getTopRight(), false);
        g.setGradientFill (gradient);
        g.fillPath (shape.front);
    }
}
void drawAbsolute (juce::Graphics& g, const KirinAttackDetail& detail,
                   juce::Rectangle<int> area, FeatureAmounts amounts, const attack_motion::Motion& motion)
{
    if (detail.shape_count >= 2) drawMembrane (g, area, amounts, motion);
}
void drawComparison (juce::Graphics& g, const KirinAttackDetail& pre,
                     const KirinAttackDetail& post, juce::Rectangle<int> area,
                     FeatureAmounts preAmounts, FeatureAmounts postAmounts,
                     const attack_motion::Motion& motion)
{
    if (pre.shape_count < 2 || post.shape_count < 2) return;
    drawMembrane (g, area, preAmounts, motion, true);
    drawMembrane (g, area, postAmounts, motion);
}
}
