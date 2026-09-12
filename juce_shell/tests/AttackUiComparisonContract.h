#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace hypha::attack_ui_test
{
enum class ComparisonFeature
{
    strength,
    texture,
    sharpness
};

inline KirinAttackDetail comparisonDetail()
{
    KirinAttackDetail detail {};
    detail.sample_rate = 48'000;
    detail.channels = 2;
    detail.event_sample = 48'000;
    detail.shape_start_sample = 43'200;
    detail.shape_end_sample = 49'440;
    detail.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY;
    detail.attack_rms_dbfs = attack_ui::strengthGlowOnDbfs;
    detail.sharpness_available = 1;
    detail.sharpness_acum = attack_ui::sharpnessGlowOnAcum;
    detail.contrast_db = attack_ui::transientGlowOnDb;
    detail.sample_edge_ratio_db = -24.0f;
    detail.crest_db = 12.0f;
    detail.peak_plateau_ms = 0.0f;
    for (std::uint32_t index = 0; index < detail.shape_count; ++index)
    {
        const auto distance = std::abs (static_cast<int> (index) - 74);
        detail.shape[index] = index < 74 ? 0.025f
            : 0.82f * std::exp (-static_cast<float> (distance) / 8.0f) + 0.018f;
    }
    return detail;
}

inline void setComparisonFeature (KirinAttackDetail& detail,
                                  ComparisonFeature feature,
                                  float normalized)
{
    const auto amount = juce::jlimit (0.0f, 1.0f, normalized);
    switch (feature)
    {
        case ComparisonFeature::strength:
            detail.attack_rms_dbfs = attack_ui::strengthGlowOnDbfs
                + amount * (attack_ui::strengthGlowFullDbfs
                            - attack_ui::strengthGlowOnDbfs);
            break;
        case ComparisonFeature::sharpness:
            detail.sharpness_acum = attack_ui::sharpnessGlowOnAcum
                + amount * (attack_ui::sharpnessGlowFullAcum
                            - attack_ui::sharpnessGlowOnAcum);
            break;
        case ComparisonFeature::texture:
        {
            const auto texture = attack_ui::textureGlowOn
                + amount * (attack_ui::textureGlowFull - attack_ui::textureGlowOn);
            detail.sample_edge_ratio_db = texture * 24.0f - 24.0f;
            detail.crest_db = 12.0f - texture * 12.0f;
            detail.peak_plateau_ms = texture * 4.0f;
            break;
        }
    }
}

inline juce::Image renderComparison (const KirinAttackDetail* pre,
                                     const KirinAttackDetail& post,
                                     const attack_motion::Motion& motion = {})
{
    juce::Image image (juce::Image::ARGB, 400, 124, true);
    juce::Graphics graphics (image);
    graphics.fillAll (juce::Colours::black);
    attack_painter::drawEventFocus (
        graphics, pre, &post, image.getBounds(), motion);
    return image;
}

inline std::uint64_t specimenLight (const juce::Image& image,
                                    juce::Rectangle<int> requested = {})
{
    const auto area = requested.isEmpty() ? image.getBounds()
                                           : requested.getIntersection (image.getBounds());
    std::uint64_t light = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto pixel = image.getPixelAt (x, y);
            light += static_cast<std::uint64_t> (pixel.getAlpha())
                   * static_cast<std::uint64_t> (pixel.getPerceivedBrightness() * 1'000.0f);
        }
    return light;
}

inline int specimenDifferences (const juce::Image& first, const juce::Image& second,
                                juce::Rectangle<int> requested = {})
{
    if (first.getBounds() != second.getBounds()) return -1;
    const auto area = requested.isEmpty() ? first.getBounds()
                                          : requested.getIntersection (first.getBounds());
    int differences = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            differences += first.getPixelAt (x, y) != second.getPixelAt (x, y);
    return differences;
}

inline bool writeComparisonPreview (const juce::Image& image,
                                    const juce::String& feature,
                                    const juce::String& direction)
{
    const auto* directory = std::getenv ("KIRIN_ATTACK_UI_SIGNED_PREVIEW_DIR");
    if (directory == nullptr) return true;
    const auto file = juce::File { directory }.getChildFile (
        "attack-" + feature + "-" + direction + ".png");
    juce::FileOutputStream output { file };
    juce::PNGImageFormat png;
    return output.openedOk() && png.writeImageToStream (image, output);
}

inline bool verifyPostAbsoluteSpecimen()
{
    constexpr std::array features {
        ComparisonFeature::strength,
        ComparisonFeature::texture,
        ComparisonFeature::sharpness,
    };
    constexpr std::array names { "strength", "texture", "sharpness" };
    auto post = comparisonDetail();
    for (const auto feature : features) setComparisonFeature (post, feature, .5f);
    auto preLow = comparisonDetail(), preHigh = comparisonDetail();
    for (const auto feature : features)
    {
        setComparisonFeature (preLow, feature, 0.0f);
        setComparisonFeature (preHigh, feature, 1.0f);
    }
    const auto base = renderComparison (&preLow, post);
    if (specimenLight (base) == 0
        || specimenDifferences (base, renderComparison (&preHigh, post)) != 0
        || specimenDifferences (base, renderComparison (nullptr, post)) != 0)
        return false; // PRE availability and PRE values cannot alter a POST specimen.

    attack_motion::Motion motion;
    motion.bend.fill (.24f);
    if (specimenDifferences (base, renderComparison (&preLow, post, motion)) != 0)
        return false; // The selected observation is static.

    for (std::size_t index = 0; index < features.size(); ++index)
    {
        auto low = post, high = post;
        setComparisonFeature (low, features[index], 0.0f);
        setComparisonFeature (high, features[index], 1.0f);
        const auto lowImage = renderComparison (&preLow, low);
        const auto highImage = renderComparison (&preLow, high);
        if (specimenDifferences (lowImage, highImage) < 80
            || ! writeComparisonPreview (lowImage, names[index], "low")
            || ! writeComparisonPreview (highImage, names[index], "high"))
            return false;
    }
    auto transientChanged = post;
    transientChanged.contrast_db += 9.0f;
    return specimenDifferences (base, renderComparison (&preLow, transientChanged)) == 0
        && writeComparisonPreview (base, "post", "absolute");
}
}
