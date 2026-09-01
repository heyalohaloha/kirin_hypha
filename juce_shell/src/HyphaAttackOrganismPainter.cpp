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

const KirinAttackDetail* findDetail (const KirinAttackDetailBatch& batch,
                                     std::int64_t eventSample) noexcept
{
    const auto count = juce::jmin (
        batch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        if (batch.details[index].event_sample == eventSample)
            return &batch.details[index];
    return nullptr;
}

float textureAmount (const KirinAttackDetail& detail) noexcept
{
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

juce::Rectangle<int> glyphBounds (std::int64_t eventSample,
                                  juce::Rectangle<int> area,
                                  std::int64_t first,
                                  std::int64_t latest)
{
    const auto localX = attack_ui::sampleX (
        eventSample, first, latest, area.getWidth());
    if (localX < 0)
        return {};
    const auto idealWidth = juce::jlimit (20, 50, area.getWidth() / 12);
    const auto width = juce::jmin (idealWidth, area.getWidth() - localX);
    const auto height = juce::jmin (
        area.getHeight() - 2,
        juce::jmax (8, static_cast<int> (std::lround (idealWidth / 2.15f))));
    if (width < 4 || height < 4)
        return {};
    return { area.getX() + localX, area.getCentreY() - height / 2, width, height };
}
}

void drawAbsoluteOverview (juce::Graphics& g, const KirinAttackDetailBatch& details,
                           juce::Rectangle<int> area, std::int64_t first,
                           std::int64_t latest, std::uint32_t rate)
{
    const auto count = juce::jmin (
        details.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& detail = details.details[index];
        if (detail.sample_rate != rate)
            continue;
        attack_overview_glyph::drawAbsolute (
            g, glyphBounds (detail.event_sample, area, first, latest),
            amounts (absoluteTint (detail)));
    }
}

void drawDifferenceOverview (juce::Graphics& g, const KirinAttackDetailBatch& preDetails,
                             const KirinAttackDetailBatch& postDetails,
                             const KirinAttackPairEventBatch& pairs,
                             juce::Rectangle<int> area, std::int64_t first,
                             std::int64_t latest, std::uint32_t rate)
{
    const auto count = juce::jmin (
        pairs.count, static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& pair = pairs.events[index];
        const auto* pre = pair.pre_available != 0
            ? findDetail (preDetails, pair.pre_event_sample) : nullptr;
        const auto* post = pair.post_available != 0
            ? findDetail (postDetails, pair.post_event_sample) : nullptr;
        if (pre == nullptr || post == nullptr
            || pre->sample_rate != rate || post->sample_rate != rate)
            continue;
        attack_overview_glyph::drawComparison (
            g, glyphBounds (pair.event_sample, area, first, latest),
            amounts (absoluteTint (*pre)), amounts (absoluteTint (*post)));
    }
}

void drawFocus (juce::Graphics& g, const KirinAttackDetail* pre,
                const KirinAttackDetail* post, juce::Rectangle<int> area)
{
    if (post == nullptr || area.getWidth() < 2 || area.getHeight() < 2)
        return;
    const auto postAmounts = amounts (absoluteTint (*post));
    if (pre == nullptr)
    {
        attack_specimen::drawAbsolute (g, *post, area, postAmounts);
        return;
    }
    attack_specimen::drawComparison (
        g, *pre, *post, area, amounts (absoluteTint (*pre)), postAmounts);
}
}
