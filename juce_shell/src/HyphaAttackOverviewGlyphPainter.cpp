#include "HyphaAttackOverviewGlyphPainter.h"
#include <cmath>
#include <functional>

namespace hypha::attack_overview_glyph
{
juce::Image Cache::lookup (attack_specimen::FeatureAmounts pre, attack_specimen::FeatureAmounts post,
                           bool paired, int width, int height, float scale, bool miniature,
                           const attack_fan::Motion* motion)
{
    if (! std::isfinite (scale) || scale <= 0 || scale > 4 || width < 4 || height < 4
        || width > 512 || height > 256) return {};
    if (motion != nullptr && std::any_of (motion->bend.begin(), motion->bend.end(),
        [] (float value) { return ! std::isfinite (value); })) return {};
    using attack_fan::unit;
    // Feature values are exact; only presentation curvature has the bounded tolerance below.
    // Generation/sample-rate changes with identical visual inputs may safely share the image.
    const std::array<float, 8> key { unit (pre.strength), unit (pre.brightness), unit (pre.transient),
        unit (pre.texture), unit (post.strength), unit (post.brightness), unit (post.transient), unit (post.texture) };
    const auto sameCurvature = [=] (const Entry& entry) {
        if (entry.animatedFocus != (motion != nullptr)) return false;
        if (motion == nullptr) return true;
        // All Bezier control-point displacements are <= 0.487 * height * max bend delta.
        // Reuse only below 0.05 PHYSICAL pixels; measured feature extents remain exact.
        const auto tolerance = .1f / (height * scale);
        for (std::size_t i = 0; i < motion->bend.size(); ++i)
            if (! std::isfinite (motion->bend[i])
                || std::abs (motion->bend[i] - entry.motion.bend[i]) > tolerance) return false;
        return true;
    };
    for (auto& entry : entries)
        if (entry.image.isValid() && entry.width == width && entry.height == height && entry.paired == paired
            && entry.miniature == miniature
            && std::equal_to<float> {} (entry.scale, scale) && entry.amounts == key && sameCurvature (entry))
        { entry.use = ++clock; return entry.image; }
    const auto pixelsWide = static_cast<int> (std::ceil (width * scale));
    const auto pixelsHigh = static_cast<int> (std::ceil (height * scale));
    const auto bytes = static_cast<std::size_t> (pixelsWide) * static_cast<std::size_t> (pixelsHigh) * 4;
    if (bytes > byteBudget) return {};
    const auto discard = [this] (Entry& entry) {
        usedBytes -= static_cast<std::size_t> (entry.image.getWidth())
                   * static_cast<std::size_t> (entry.image.getHeight()) * 4;
        entry = {}; };
    const auto oldest = [this] {
        return std::min_element (entries.begin(), entries.end(),
            [] (const auto& a, const auto& b) { return a.use < b.use; }); };
    while (usedBytes + bytes > byteBudget)
    {
        auto victim = std::min_element (entries.begin(), entries.end(), [] (const auto& a, const auto& b) {
            return (a.image.isValid() ? a.use : UINT64_MAX) < (b.image.isValid() ? b.use : UINT64_MAX); });
        discard (*victim);
    }
    auto& slot = *oldest();
    if (slot.image.isValid()) discard (slot);
    slot.image = juce::Image (juce::Image::ARGB, pixelsWide, pixelsHigh, true);
    if (! slot.image.isValid()) return {};
    {
        juce::Graphics raster (slot.image);
        raster.addTransform (juce::AffineTransform::scale (scale));
        const auto bend = motion != nullptr ? *motion : attack_fan::Motion {};
        if (paired) attack_specimen::drawFan (raster, { 0, 0, width, height }, pre, bend, true, miniature);
        attack_specimen::drawFan (raster, { 0, 0, width, height }, post, bend, false, miniature);
    }
    slot.amounts = key; slot.width = width; slot.height = height; slot.scale = scale;
    slot.paired = paired; slot.miniature = miniature;
    slot.animatedFocus = motion != nullptr; slot.motion = motion != nullptr ? *motion : attack_fan::Motion {};
    slot.use = ++clock; usedBytes += bytes; ++buildCount;
    return slot.image;
}
namespace
{
bool cached (juce::Graphics& g, juce::Rectangle<int> area, attack_specimen::FeatureAmounts pre,
             attack_specimen::FeatureAmounts post, bool paired, Cache* cache, bool miniature = true,
             const attack_fan::Motion* motion = nullptr)
{
    if (cache == nullptr) return false;
    const auto image = cache->lookup (pre, post, paired, area.getWidth(), area.getHeight(),
                                     g.getInternalContext().getPhysicalPixelScaleFactor(), miniature, motion);
    if (! image.isValid()) return false;
    juce::Graphics::ScopedSaveState saved (g);
    g.setOpacity (1.0f);
    g.drawImage (image, area.toFloat());
    return true;
}
}
void drawAbsolute (juce::Graphics& g, juce::Rectangle<int> area,
                   attack_specimen::FeatureAmounts amounts, Cache* cache)
{
    if (cached (g, area, {}, amounts, false, cache)) return;
    attack_specimen::drawFan (g, area, amounts, {}, false, true);
}
void drawComparison (juce::Graphics& g, juce::Rectangle<int> area,
                     attack_specimen::FeatureAmounts preAmounts,
                     attack_specimen::FeatureAmounts postAmounts, Cache* cache)
{
    if (cached (g, area, preAmounts, postAmounts, true, cache)) return;
    attack_specimen::drawFan (g, area, preAmounts, {}, true, true);
    attack_specimen::drawFan (g, area, postAmounts, {}, false, true);
}
void drawFocus (juce::Graphics& g, juce::Rectangle<int> area,
                 attack_specimen::FeatureAmounts pre, attack_specimen::FeatureAmounts post,
                 bool paired, const attack_fan::Motion& motion, Cache* cache)
{
    if (cached (g, area, pre, post, paired, cache, false, &motion)) return;
    if (paired) attack_specimen::drawFan (g, area, pre, motion, true);
    attack_specimen::drawFan (g, area, post, motion);
}
}
