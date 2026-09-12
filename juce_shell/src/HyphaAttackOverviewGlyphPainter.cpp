#include "HyphaAttackOverviewGlyphPainter.h"
#include <cmath>
#include <functional>

namespace hypha::attack_focus
{
namespace
{
float stableAmount (float value) noexcept
{
    constexpr float steps = 48.0f;
    return std::round (attack_motion::unit (value) * steps) / steps;
}
}

juce::Image Cache::lookup (attack_specimen::FeatureAmounts pre, attack_specimen::FeatureAmounts post,
                           bool isPaired, int w, int h, float dpi, const attack_motion::Motion& bend)
{
    if (! std::isfinite (dpi) || dpi <= 0 || dpi > 4 || w < 4 || h < 4 || w > 1024 || h > 512)
        return {};
    juce::ignoreUnused (pre, isPaired, bend);
    const std::array<float, 3> key {
        stableAmount (post.strength), stableAmount (post.texture), stableAmount (post.sharpness) };
    if (image.isValid() && width == w && height == h && std::equal_to<float> {} (scale, dpi)
        && amounts == key) return image;
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
        attack_specimen::drawSpecimen (raster, { 0, 0, w, h }, { key[0], key[1], key[2] });
    }
    image = next; width = w; height = h; scale = dpi; amounts = key;
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
    juce::ignoreUnused (pre, paired, motion);
    attack_specimen::drawSpecimen (g, area, post);
}
}
