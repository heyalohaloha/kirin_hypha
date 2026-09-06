#include "HyphaAttackOrganismPainter.h"

#include <cmath>

#include "HyphaAttackOverviewGlyphPainter.h"
#include "HyphaAttackSpecimenPainter.h"
#include "HyphaAttackUiContract.h"

namespace hypha::attack_organism
{
namespace
{
struct FeatureTint
{
    float strength = 0.0f;
    float brightness = 0.0f;
    float transient = 0.0f;
    float texture = 0.0f;
};

float textureAmount (const KirinAttackDetail& detail) noexcept
{
    if (! std::isfinite (detail.sample_edge_ratio_db) || ! std::isfinite (detail.crest_db)
        || ! std::isfinite (detail.peak_plateau_ms))
        return 0.0f;
    const auto edge = juce::jlimit (
        0.0f, 1.0f, (detail.sample_edge_ratio_db + 24.0f) / 24.0f);
    const auto density = juce::jlimit (0.0f, 1.0f, (12.0f - detail.crest_db) / 12.0f);
    const auto plateau = juce::jlimit (0.0f, 1.0f, detail.peak_plateau_ms / 4.0f);
    return juce::jmin (edge, density, plateau);
}

float glowAmount (float value, float onset, float full) noexcept
{
    if (! std::isfinite (value) || full <= onset || value <= onset)
        return 0.0f;
    const auto amount = juce::jlimit (0.0f, 1.0f, (value - onset) / (full - onset));
    return amount * amount * (3.0f - 2.0f * amount);
}

FeatureTint absoluteTint (const KirinAttackDetail& detail) noexcept
{
    return {
        glowAmount (detail.attack_rms_dbfs, attack_ui::strengthGlowOnDbfs,
                    attack_ui::strengthGlowFullDbfs),
        detail.sharpness_available != 0
            ? glowAmount (detail.sharpness_acum, attack_ui::brightnessGlowOnAcum,
                          attack_ui::brightnessGlowFullAcum) : 0.0f,
        glowAmount (detail.contrast_db, attack_ui::transientGlowOnDb,
                    attack_ui::transientGlowFullDb),
        glowAmount (textureAmount (detail), attack_ui::textureGlowOn,
                    attack_ui::textureGlowFull)
    };
}

attack_specimen::FeatureAmounts amounts (FeatureTint tint) noexcept
{
    return { tint.strength, tint.brightness, tint.transient, tint.texture };
}

}

void drawFocus (juce::Graphics& g, const KirinAttackDetail* pre,
                const KirinAttackDetail* post, juce::Rectangle<int> area,
                const attack_motion::Motion& motion, attack_focus::Cache* cache)
{
    if (post == nullptr || post->shape_count < 2 || (pre != nullptr && pre->shape_count < 2)
        || area.getWidth() < 2 || area.getHeight() < 2)
        return;
    const auto postAmounts = amounts (absoluteTint (*post));
    attack_focus::drawFocus (g, area,
        pre != nullptr ? amounts (absoluteTint (*pre)) : attack_specimen::FeatureAmounts {},
        postAmounts, pre != nullptr, motion, cache);
}
}
