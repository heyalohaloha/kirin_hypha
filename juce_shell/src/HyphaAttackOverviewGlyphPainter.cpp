#include "HyphaAttackOverviewGlyphPainter.h"
#include <cmath>
#include <functional>

namespace hypha::attack_focus
{
juce::Image Cache::lookup (attack_specimen::FeatureAmounts pre, attack_specimen::FeatureAmounts post,
                           bool isPaired, int w, int h, float dpi, const attack_motion::Motion& bend)
{
    if (! std::isfinite (dpi) || dpi <= 0 || dpi > 4 || w < 4 || h < 4 || w > 1024 || h > 512
        || std::any_of (bend.bend.begin(), bend.bend.end(), [] (float v) { return ! std::isfinite (v); }))
        return {};
    using attack_motion::unit;
    if (! isPaired) pre = {};
    const std::array<float, 8> key { unit (pre.strength), unit (pre.brightness), unit (pre.transient),
        unit (pre.texture), unit (post.strength), unit (post.brightness), unit (post.transient), unit (post.texture) };
    // Control points and gradient anchors move <= 0.1 * height * maximum bend delta.
    // Catmull-Rom control amplification is <= 4/3; 0.07 * 4/3 < 0.1.
    // Reuse stays below 0.05 physical pixels, without quantizing measured features.
    const auto tolerance = .5f / (static_cast<float> (h) * dpi);
    bool sameBend = true;
    for (std::size_t i = 0; i < bend.bend.size(); ++i)
        sameBend = sameBend && std::abs (bend.bend[i] - motion.bend[i]) <= tolerance;
    if (image.isValid() && width == w && height == h && std::equal_to<float> {} (scale, dpi)
        && paired == isPaired && amounts == key && sameBend) return image;
    const auto pw = static_cast<int> (std::ceil (w * dpi)), ph = static_cast<int> (std::ceil (h * dpi));
    const auto bytesNeeded = static_cast<std::size_t> (pw) * static_cast<std::size_t> (ph) * 4;
    if (bytesNeeded > byteBudget) return {};
    // Retain only one focus. Animation cannot evict/rebuild hundreds of history images.
    image = {}; usedBytes = 0;
    juce::Image next (juce::Image::ARGB, pw, ph, true);
    if (! next.isValid()) return {};
    {
        juce::Graphics raster (next);
        raster.addTransform (juce::AffineTransform::scale (dpi));
        if (isPaired) attack_specimen::drawMembrane (raster, { 0, 0, w, h }, pre, bend, true);
        attack_specimen::drawMembrane (raster, { 0, 0, w, h }, post, bend);
    }
    image = next; width = w; height = h; scale = dpi; paired = isPaired; amounts = key; motion = bend;
    usedBytes = bytesNeeded; ++buildCount;
    return image;
}
void drawFocus (juce::Graphics& g, juce::Rectangle<int> area,
                 attack_specimen::FeatureAmounts pre, attack_specimen::FeatureAmounts post,
                 bool paired, const attack_motion::Motion& motion, Cache* cache)
{
    if (cache != nullptr)
    {
        const auto dpi = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto image = cache->lookup (pre, post, paired, area.getWidth(), area.getHeight(),
            dpi, motion);
        if (image.isValid())
        {
            juce::Graphics::ScopedSaveState saved (g);
            g.reduceClipRegion (area);
            g.setOpacity (1);
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            // Ceil-sized backing images include a partial final pixel at fractional DPI.
            // Preserve their physical scale instead of shrinking that padding into the geometry.
            g.drawImage (image, {static_cast<float> (area.getX()), static_cast<float> (area.getY()),
                static_cast<float> (image.getWidth())/dpi, static_cast<float> (image.getHeight())/dpi});
            return;
        }
    }
    if (paired) attack_specimen::drawMembrane (g, area, pre, motion, true);
    attack_specimen::drawMembrane (g, area, post, motion);
}
}
