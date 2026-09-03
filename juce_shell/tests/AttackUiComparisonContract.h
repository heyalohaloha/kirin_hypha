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
    brightness,
    transient,
    texture
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
    detail.sharpness_acum = attack_ui::brightnessGlowOnAcum;
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
        case ComparisonFeature::brightness:
            detail.sharpness_acum = attack_ui::brightnessGlowOnAcum
                + amount * (attack_ui::brightnessGlowFullAcum
                            - attack_ui::brightnessGlowOnAcum);
            break;
        case ComparisonFeature::transient:
            detail.contrast_db = attack_ui::transientGlowOnDb
                + amount * (attack_ui::transientGlowFullDb
                            - attack_ui::transientGlowOnDb);
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

inline juce::Image renderComparison (const KirinAttackDetail& pre,
                                     const KirinAttackDetail& post,
                                     float emissionPhase = 0.0f)
{
    juce::Image image (juce::Image::ARGB, 300, 100, true);
    juce::Graphics graphics (image);
    graphics.fillAll (juce::Colours::black);
    attack_painter::drawEventFocus (
        graphics, &pre, &post, image.getBounds(), emissionPhase);
    return image;
}

inline std::uint64_t specimenLight (const juce::Image& image)
{
    std::uint64_t light = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto pixel = image.getPixelAt (x, y);
            light += static_cast<std::uint64_t> (pixel.getAlpha())
                   * static_cast<std::uint64_t> (pixel.getPerceivedBrightness() * 1'000.0f);
        }
    return light;
}

inline int specimenDifferences (const juce::Image& first, const juce::Image& second)
{
    int differences = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            differences += first.getPixelAt (x, y) != second.getPixelAt (x, y);
    return differences;
}

inline bool writeComparisonPreview (const juce::Image& image,
                                    const juce::String& feature,
                                    const juce::String& direction)
{
    const auto* directory = std::getenv ("KIRIN_ATTACK_UI_SIGNED_PREVIEW_DIR");
    if (directory == nullptr)
        return true;
    const auto file = juce::File { directory }.getChildFile (
        "attack-" + feature + "-" + direction + ".png");
    juce::FileOutputStream output { file };
    juce::PNGImageFormat png;
    return output.openedOk() && png.writeImageToStream (image, output);
}

inline bool verifySignedComparisonSpecimen()
{
    constexpr std::array features {
        ComparisonFeature::strength,
        ComparisonFeature::brightness,
        ComparisonFeature::transient,
        ComparisonFeature::texture,
    };
    constexpr std::array names { "strength", "brightness", "transient", "texture" };
    for (std::size_t index = 0; index < features.size(); ++index)
    {
        const auto feature = features[index];
        auto pre = comparisonDetail();
        auto positive = comparisonDetail();
        auto negative = comparisonDetail();
        setComparisonFeature (pre, feature, 0.50f);
        setComparisonFeature (positive, feature, 0.75f);
        setComparisonFeature (negative, feature, 0.25f);
        const auto identity = renderComparison (pre, pre);
        const auto positiveImage = renderComparison (pre, positive);
        const auto negativeImage = renderComparison (pre, negative);
        if (specimenLight (identity) == 0
            || specimenLight (positiveImage) <= specimenLight (negativeImage)
            || specimenDifferences (positiveImage, negativeImage) < 100
            || ! writeComparisonPreview (positiveImage, names[index], "positive")
            || ! writeComparisonPreview (negativeImage, names[index], "negative"))
            return false;
    }
    auto mixedPre = comparisonDetail();
    auto mixedPost = comparisonDetail();
    for (const auto feature : features)
    {
        setComparisonFeature (mixedPre, feature, 0.50f);
        setComparisonFeature (mixedPost, feature,
                              feature == ComparisonFeature::strength
                                  || feature == ComparisonFeature::transient ? 0.75f : 0.25f);
    }
    const auto identityImage = renderComparison (mixedPre, mixedPre);
    const auto mixedImage = renderComparison (mixedPre, mixedPost);
    if (specimenDifferences (identityImage, mixedImage) < 100)
        return false;
    if (specimenDifferences (mixedImage, renderComparison (mixedPre, mixedPost, 0.5f)) < 100)
        return false;
    if (! writeComparisonPreview (mixedImage, "mixed", "signed")
        || ! writeComparisonPreview (identityImage, "identity", "zero"))
        return false;
    return true;
}
}
