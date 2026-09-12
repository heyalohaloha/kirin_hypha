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
    float texture = 0.0f;
    float sharpness = 0.0f;
};

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
        glowAmount (textureAmount (detail), attack_ui::textureGlowOn,
                    attack_ui::textureGlowFull),
        detail.sharpness_available != 0
            ? glowAmount (detail.sharpness_acum, attack_ui::sharpnessGlowOnAcum,
                          attack_ui::sharpnessGlowFullAcum) : 0.0f
    };
}

attack_specimen::FeatureAmounts amounts (FeatureTint tint) noexcept
{
    return { tint.strength, tint.texture, tint.sharpness };
}

}

bool textureAvailable (const KirinAttackDetail& detail) noexcept
{
    return std::isfinite (detail.sample_edge_ratio_db) && std::isfinite (detail.crest_db)
        && std::isfinite (detail.peak_plateau_ms);
}

float textureAmount (const KirinAttackDetail& detail) noexcept
{
    if (! textureAvailable (detail)) return 0.0f;
    const auto edge = juce::jlimit (
        0.0f, 1.0f, (detail.sample_edge_ratio_db + 24.0f) / 24.0f);
    const auto density = juce::jlimit (0.0f, 1.0f, (12.0f - detail.crest_db) / 12.0f);
    const auto plateau = juce::jlimit (0.0f, 1.0f, detail.peak_plateau_ms / 4.0f);
    return juce::jmin (edge, density, plateau);
}

void drawFocus (juce::Graphics& g, const KirinAttackDetail* pre,
                const KirinAttackDetail* post, juce::Rectangle<int> area,
                const attack_motion::Motion&, attack_focus::Cache* cache)
{
    juce::ignoreUnused (pre);
    if (post == nullptr || post->shape_count < 2 || area.getWidth() < 2 || area.getHeight() < 2)
        return;
    const auto postAmounts = amounts (absoluteTint (*post));
    attack_focus::drawFocus (g, area, {}, postAmounts, false, {}, cache);
}
}
