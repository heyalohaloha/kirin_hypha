#pragma once

#include <cmath>
#include <cstdint>

namespace hypha::attack_ui_test
{
inline KirinAttackDetail overviewDetail()
{
    KirinAttackDetail detail {};
    detail.sample_rate = 48'000;
    detail.channels = 2;
    detail.event_sample = 144'000;
    detail.shape_start_sample = detail.event_sample - 4'800;
    detail.shape_end_sample = detail.event_sample + 1'440;
    detail.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY;
    detail.attack_rms_dbfs = attack_ui::strengthGlowFullDbfs;
    detail.sharpness_available = 1;
    detail.sharpness_acum = attack_ui::brightnessGlowFullAcum;
    detail.contrast_db = attack_ui::transientGlowFullDb;
    detail.sample_edge_ratio_db = 0.0f;
    detail.crest_db = 0.0f;
    detail.peak_plateau_ms = 4.0f;
    for (std::uint32_t index = 0; index < detail.shape_count; ++index)
    {
        const auto distance = std::abs (static_cast<int> (index) - 74);
        detail.shape[index] = index < 74 ? 0.03f
            : 0.88f * std::exp (-static_cast<float> (distance) / 8.0f) + 0.02f;
    }
    return detail;
}

inline bool verifyNoPreOnsetFeatureInk()
{
    constexpr int width = 600;
    constexpr int height = 100;
    constexpr std::int64_t latest = 288'000;
    constexpr std::uint32_t rate = 48'000;
    KirinAttackWaveformBatch waveform {};
    KirinAttackDetailBatch details {};
    details.capacity = KIRIN_ATTACK_DETAIL_BATCH_CAPACITY;
    details.count = 1;
    details.details[0] = overviewDetail();
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    attack_painter::drawWaveform (
        graphics, waveform, details, image.getBounds(), 0, latest, rate,
        attack_painter::WaveformStyle::continuous, true, 1.0f);
    const auto onset = attack_ui::eventX (
        details.details[0].event_sample, latest, rate, width);
    int before = 0;
    int after = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            if (image.getPixelAt (x, y).getAlpha() == 0)
                continue;
            if (x < onset - 1)
                ++before;
            else if (x > onset)
                ++after;
        }
    return onset > 0 && before == 0 && after > 0;
}

inline bool verifyAsymmetricMeasuredFlow()
{
    constexpr int width = 600;
    constexpr int height = 100;
    constexpr std::int64_t latest = 288'000;
    constexpr std::uint32_t rate = 48'000;
    KirinAttackWaveformBatch waveform {};
    waveform.capacity = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    waveform.count = 300;
    for (std::uint32_t index = 0; index < waveform.count; ++index)
    {
        auto& point = waveform.points[index];
        point.sample_rate = rate;
        point.start_sample = static_cast<std::int64_t> (index) * 960;
        point.end_sample = point.start_sample + 960;
        point.rms_dbfs = -18.0f + 3.0f * std::sin (static_cast<float> (index) * 0.071f);
    }
    KirinAttackDetailBatch details {};
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    attack_painter::drawWaveform (
        graphics, waveform, details, image.getBounds(), 0, latest, rate,
        attack_painter::WaveformStyle::continuous, false, 1.0f);
    int visible = 0;
    int mirroredDifferences = 0;
    for (int y = 0; y < height / 2; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto top = image.getPixelAt (x, y);
            const auto bottom = image.getPixelAt (x, height - 1 - y);
            visible += top.getAlpha() > 0 || bottom.getAlpha() > 0;
            mirroredDifferences += top != bottom;
        }
    return visible > 100 && mirroredDifferences > visible / 3;
}

inline juce::Image renderOverviewComparison (const KirinAttackDetail& pre,
                                             const KirinAttackDetail& post)
{
    constexpr int width = 600;
    constexpr int height = 100;
    KirinAttackDetailBatch preDetails {};
    KirinAttackDetailBatch postDetails {};
    preDetails.count = 1;
    postDetails.count = 1;
    preDetails.details[0] = pre;
    postDetails.details[0] = post;
    KirinAttackPairEventBatch pairs {};
    pairs.count = 1;
    pairs.events[0].event_sample = post.event_sample;
    pairs.events[0].pre_event_sample = pre.event_sample;
    pairs.events[0].post_event_sample = post.event_sample;
    pairs.events[0].pre_available = 1;
    pairs.events[0].post_available = 1;
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    attack_painter::drawWaveformDifferences (
        graphics, preDetails, postDetails, pairs, image.getBounds(),
        0, 288'000, 48'000);
    return image;
}

inline bool verifySignedOverviewGlyph()
{
    auto pre = comparisonDetail();
    auto positive = comparisonDetail();
    auto negative = comparisonDetail();
    for (const auto feature : { ComparisonFeature::strength,
                                ComparisonFeature::brightness,
                                ComparisonFeature::transient,
                                ComparisonFeature::texture })
    {
        setComparisonFeature (pre, feature, 0.50f);
        setComparisonFeature (positive, feature, 0.75f);
        setComparisonFeature (negative, feature, 0.25f);
    }
    const auto identity = renderOverviewComparison (pre, pre);
    const auto positiveImage = renderOverviewComparison (pre, positive);
    const auto negativeImage = renderOverviewComparison (pre, negative);
    return specimenLight (identity) > 0
        && specimenLight (positiveImage) > specimenLight (negativeImage)
        && specimenDifferences (positiveImage, negativeImage) > 100;
}
}
