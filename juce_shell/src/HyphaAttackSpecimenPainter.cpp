#include "HyphaAttackSpecimenPainter.h"
#include "HyphaAttackFanGeometry.h"
#include "HyphaAttackUiContract.h"

namespace hypha::attack_specimen
{
void drawFan (juce::Graphics& g, juce::Rectangle<int> area, FeatureAmounts raw,
              const attack_fan::Motion& motion, bool reference, bool miniature)
{
    using attack_fan::unit;
    const FeatureAmounts a { unit (raw.strength), unit (raw.brightness),
                              unit (raw.transient), unit (raw.texture) };
    if (area.getWidth() < 4 || area.getHeight() < 4
        || (a.strength <= 0 && a.brightness <= 0 && a.transient <= 0 && a.texture <= 0))
        return;
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);
    const auto shape = attack_fan::geometry (area.toFloat().reduced (1.0f), a, motion, miniature);
    const auto gradient = [&] (std::uint32_t rgb, float opacity)
    {
        const auto c = reference ? juce::Colour (0xffa3b3b9) : juce::Colour (rgb);
        const auto alpha = opacity * (reference ? 0.32f : 1.0f);
        juce::ColourGradient fade (c.withAlpha (0.0f), static_cast<float> (area.getX()),
            static_cast<float> (area.getCentreY()), c.withAlpha (alpha * 0.40f),
            static_cast<float> (area.getRight()), static_cast<float> (area.getCentreY()), false);
        fade.addColour (0.18, c.withAlpha (alpha * 0.30f));
        fade.addColour (0.54, c.withAlpha (alpha * 0.72f));
        fade.addColour (0.77, c.withAlpha (alpha));
        g.setGradientFill (fade);
    };
    const auto stroke = [&] (const juce::Path& path, std::uint32_t rgb, float alpha, float width)
    {
        gradient (rgb, alpha);
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    };
    // Finite, continuous gradients replace raster resampling and repeated blur/feather passes.
    if (a.brightness > 0)
        stroke (shape.ribs, attack_ui::brightnessColour, a.brightness * 0.92f,
                miniature ? 0.65f : 0.90f);
    if (a.texture > 0)
        stroke (shape.branches, attack_ui::textureColour, a.texture * 0.92f,
                miniature ? 0.48f : 0.68f);
    if (a.strength > 0)
    {
        if (! reference)
        {
            gradient (attack_ui::strengthColour, a.strength * 0.54f);
            g.fillPath (shape.root);
        }
        stroke (shape.root, attack_ui::strengthColour, a.strength * 0.95f,
                miniature ? 0.55f : 0.90f);
    }
    if (a.transient > 0)
        stroke (shape.front, attack_ui::transientColour, a.transient,
                miniature ? 0.65f : 1.10f);
}

void drawAbsolute (juce::Graphics& g, const KirinAttackDetail& detail,
                   juce::Rectangle<int> area, FeatureAmounts amounts, const attack_fan::Motion& motion)
{
    if (detail.shape_count >= 2) drawFan (g, area, amounts, motion);
}

void drawComparison (juce::Graphics& g, const KirinAttackDetail& pre,
                     const KirinAttackDetail& post, juce::Rectangle<int> area,
                     FeatureAmounts preAmounts, FeatureAmounts postAmounts,
                     const attack_fan::Motion& motion)
{
    if (pre.shape_count < 2 || post.shape_count < 2) return;
    // Identical scales and shared presentation curvature: motion cannot invent a PRE/POST delta.
    // Signed numeric deltas are primary. The fan is not a literal waveform or duration.
    drawFan (g, area, preAmounts, motion, true);
    drawFan (g, area, postAmounts, motion);
}
}
