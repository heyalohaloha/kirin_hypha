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
                                     std::int64_t eventSample, std::uint64_t generation,
                                     std::uint32_t sampleRate) noexcept
{
    const auto count = juce::jmin (
        batch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        if (batch.details[index].event_sample == eventSample
            && batch.details[index].generation == generation
            && batch.details[index].sample_rate == sampleRate)
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
                                  std::int64_t latest, int visibleEvents)
{
    const auto localX = attack_ui::sampleX (
        eventSample, first, latest, area.getWidth());
    if (localX < 0)
        return {};
    const auto idealWidth = juce::jlimit (20, 88, area.getWidth() / juce::jmax (10, visibleEvents));
    const auto width = idealWidth; // Clip the moving plate; never squash it at NOW.
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
                           std::int64_t latest, std::uint32_t rate, attack_overview_glyph::Cache* cache)
{
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);
    const auto count = juce::jmin (
        details.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    const auto visible = static_cast<int> (std::count_if (details.details, details.details + count,
        [=] (const auto& d) { return d.sample_rate == rate && d.event_sample >= first && d.event_sample <= latest; }));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& detail = details.details[index];
        if (detail.sample_rate != rate)
            continue;
        attack_overview_glyph::drawAbsolute (
            g, glyphBounds (detail.event_sample, area, first, latest, visible),
            amounts (absoluteTint (detail)), cache);
    }
}

void drawDifferenceOverview (juce::Graphics& g, const KirinAttackDetailBatch& preDetails,
                             const KirinAttackDetailBatch& postDetails,
                             const KirinAttackPairEventBatch& pairs,
                             juce::Rectangle<int> area, std::int64_t first,
                             std::int64_t latest, std::uint32_t rate, attack_overview_glyph::Cache* cache)
{
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);
    const auto count = juce::jmin (
        pairs.count, static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
    const auto visible = static_cast<int> (std::count_if (pairs.events, pairs.events + count,
        [=] (const auto& p) { return p.sample_rate == rate && p.event_sample >= first && p.event_sample <= latest; }));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& pair = pairs.events[index];
        const auto* pre = pair.pre_available != 0
            ? findDetail (preDetails, pair.pre_event_sample, pair.pre_generation, pair.sample_rate) : nullptr;
        const auto* post = pair.post_available != 0
            ? findDetail (postDetails, pair.post_event_sample, pair.post_generation, pair.sample_rate) : nullptr;
        if (pre == nullptr || post == nullptr
            || pre->sample_rate != rate || post->sample_rate != rate)
            continue;
        attack_overview_glyph::drawComparison (
            g, glyphBounds (pair.event_sample, area, first, latest, visible),
            amounts (absoluteTint (*pre)), amounts (absoluteTint (*post)), cache);
    }
}

void drawFocus (juce::Graphics& g, const KirinAttackDetail* pre,
                const KirinAttackDetail* post, juce::Rectangle<int> area,
                const attack_fan::Motion& motion, attack_overview_glyph::Cache* cache)
{
    if (post == nullptr || post->shape_count < 2 || (pre != nullptr && pre->shape_count < 2)
        || area.getWidth() < 2 || area.getHeight() < 2)
        return;
    const auto postAmounts = amounts (absoluteTint (*post));
    attack_overview_glyph::drawFocus (g, area,
        pre != nullptr ? amounts (absoluteTint (*pre)) : attack_specimen::FeatureAmounts {},
        postAmounts, pre != nullptr, motion, cache);
}
}
