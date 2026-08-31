#include "HyphaAttackPainter.h"

#include <cmath>
#include <vector>

#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha::attack_painter
{
namespace
{
const auto waveformColour = juce::Colour (attack_ui::waveformColour);
const auto strengthColour = juce::Colour (attack_ui::strengthColour);
const auto brightnessColour = juce::Colour (attack_ui::brightnessColour);
const auto transientColour = juce::Colour (attack_ui::transientColour);
const auto textureColour = juce::Colour (attack_ui::textureColour);
const auto panelColour = juce::Colour (0xff111722);

struct FeatureTint
{
    float strength = 0.0f;
    float brightness = 0.0f;
    float transient = 0.0f;
    float texture = 0.0f;
};

struct PaintedPoint
{
    float x = 0.0f;
    float height = 0.0f;
    FeatureTint tint {};
};

float levelHeight (float db, float halfHeight)
{
    const auto normalized = juce::jlimit (
        0.0f, 1.0f, (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb);
    return normalized * juce::jmax (0.0f, halfHeight - 1.0f);
}

std::int64_t sampleDistance (std::int64_t first, std::int64_t second) noexcept
{
    return first > second ? first - second : second - first;
}

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
    const auto density = juce::jlimit (
        0.0f, 1.0f, (12.0f - detail.crest_db) / 12.0f);
    const auto plateau = juce::jlimit (0.0f, 1.0f, detail.peak_plateau_ms / 4.0f);
    return juce::jmin (edge, density, plateau);
}

float smoothAmount (float amount) noexcept
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, amount);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

float glowAmount (float value, float onset, float full) noexcept
{
    if (! std::isfinite (value) || full <= onset || value <= onset)
        return 0.0f;
    return smoothAmount ((value - onset) / (full - onset));
}

FeatureTint absoluteTint (const KirinAttackDetailBatch& details,
                          std::int64_t sample,
                          std::uint32_t rate) noexcept
{
    const auto radius = static_cast<std::int64_t> (rate)
                      * attack_ui::featureTintRadiusMs / 1'000;
    const KirinAttackDetail* nearest = nullptr;
    auto nearestDistance = radius + 1;
    const auto count = juce::jmin (
        details.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& detail = details.details[index];
        const auto distance = sampleDistance (sample, detail.event_sample);
        if (detail.sample_rate == rate && distance <= radius && distance < nearestDistance)
        {
            nearest = &detail;
            nearestDistance = distance;
        }
    }
    if (nearest == nullptr || radius <= 0)
        return {};
    const auto proximity = smoothAmount (
        1.0f - static_cast<float> (nearestDistance) / static_cast<float> (radius));
    return {
        glowAmount (nearest->attack_rms_dbfs,
                    attack_ui::strengthGlowOnDbfs,
                    attack_ui::strengthGlowFullDbfs) * proximity,
        nearest->sharpness_available != 0
            ? glowAmount (nearest->sharpness_acum,
                          attack_ui::brightnessGlowOnAcum,
                          attack_ui::brightnessGlowFullAcum) * proximity
            : 0.0f,
        glowAmount (nearest->contrast_db,
                    attack_ui::transientGlowOnDb,
                    attack_ui::transientGlowFullDb) * proximity,
        glowAmount (textureAmount (*nearest),
                    attack_ui::textureGlowOn,
                    attack_ui::textureGlowFull) * proximity
    };
}

FeatureTint differenceTint (const KirinAttackDetailBatch& preDetails,
                            const KirinAttackDetailBatch& postDetails,
                            const KirinAttackPairEventBatch& pairs,
                            std::int64_t sample,
                            std::uint32_t rate) noexcept
{
    const auto radius = static_cast<std::int64_t> (rate)
                      * attack_ui::featureTintRadiusMs / 1'000;
    const KirinAttackPairEvent* nearest = nullptr;
    auto nearestDistance = radius + 1;
    const auto count = juce::jmin (
        pairs.count, static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& pair = pairs.events[index];
        const auto distance = sampleDistance (sample, pair.event_sample);
        if (pair.pre_available != 0 && pair.post_available != 0
            && distance <= radius && distance < nearestDistance)
        {
            nearest = &pair;
            nearestDistance = distance;
        }
    }
    if (nearest == nullptr || radius <= 0)
        return {};
    const auto* pre = findDetail (preDetails, nearest->pre_event_sample);
    const auto* post = findDetail (postDetails, nearest->post_event_sample);
    if (pre == nullptr || post == nullptr)
        return {};
    const auto proximity = smoothAmount (
        1.0f - static_cast<float> (nearestDistance) / static_cast<float> (radius));
    const auto brightnessAvailable = pre->sharpness_available != 0
                                  && post->sharpness_available != 0;
    return {
        glowAmount (std::abs (post->attack_rms_dbfs - pre->attack_rms_dbfs),
                    attack_ui::strengthDifferenceGlowOnDb,
                    attack_ui::strengthDifferenceGlowFullDb) * proximity,
        brightnessAvailable
            ? glowAmount (std::abs (post->sharpness_acum - pre->sharpness_acum),
                          attack_ui::brightnessDifferenceGlowOnAcum,
                          attack_ui::brightnessDifferenceGlowFullAcum) * proximity
            : 0.0f,
        glowAmount (std::abs (post->contrast_db - pre->contrast_db),
                    attack_ui::transientDifferenceGlowOnDb,
                    attack_ui::transientDifferenceGlowFullDb) * proximity,
        glowAmount (std::abs (textureAmount (*post) - textureAmount (*pre)),
                    attack_ui::textureDifferenceGlowOn,
                    attack_ui::textureDifferenceGlowFull) * proximity
    };
}

template <typename TintProvider>
std::vector<PaintedPoint> collectPoints (const KirinAttackWaveformBatch& batch,
                                         juce::Rectangle<int> area,
                                         std::int64_t first,
                                         std::int64_t latest,
                                         std::uint32_t rate,
                                         TintProvider tintProvider)
{
    std::vector<PaintedPoint> points;
    const auto count = juce::jmin (
        batch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY));
    points.reserve (count);
    const auto halfHeight = area.getHeight() * 0.5f - 2.0f;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& point = batch.points[index];
        if (point.sample_rate != rate)
            continue;
        const auto sample = point.start_sample + (point.end_sample - point.start_sample) / 2;
        const auto localX = attack_ui::sampleX (sample, first, latest, area.getWidth());
        if (localX >= 0)
            points.push_back ({ static_cast<float> (area.getX() + localX),
                                levelHeight (point.rms_dbfs, halfHeight),
                                tintProvider (sample) });
    }
    return points;
}

template <typename Extent>
void appendSmoothContour (juce::Path& path,
                          const std::vector<PaintedPoint>& points,
                          float centreY,
                          Extent extent,
                          bool reverse,
                          bool startNew)
{
    if (points.empty())
        return;
    const auto pointAt = [&] (std::size_t logicalIndex) -> const PaintedPoint&
    {
        return reverse ? points[points.size() - 1 - logicalIndex] : points[logicalIndex];
    };
    const auto yAt = [&] (const PaintedPoint& point) { return centreY + extent (point); };
    const auto& first = pointAt (0);
    if (startNew)
        path.startNewSubPath (first.x, yAt (first));
    else
        path.lineTo (first.x, yAt (first));
    if (points.size() == 1)
        return;
    const auto& second = pointAt (1);
    path.lineTo ((first.x + second.x) * 0.5f, (yAt (first) + yAt (second)) * 0.5f);
    for (std::size_t index = 1; index + 1 < points.size(); ++index)
    {
        const auto& point = pointAt (index);
        const auto& next = pointAt (index + 1);
        path.quadraticTo (point.x, yAt (point),
                          (point.x + next.x) * 0.5f, (yAt (point) + yAt (next)) * 0.5f);
    }
    const auto& last = pointAt (points.size() - 1);
    path.lineTo (last.x, yAt (last));
}

template <typename Extent>
juce::Path symmetricBody (const std::vector<PaintedPoint>& points,
                          float centreY,
                          Extent extent)
{
    juce::Path path;
    if (points.empty())
        return path;
    appendSmoothContour (path, points, centreY,
                         [&] (const PaintedPoint& point) { return -extent (point); },
                         false, true);
    appendSmoothContour (path, points, centreY, extent, true, false);
    path.closeSubPath();
    return path;
}

void gradientFill (juce::Graphics& g,
                   const juce::Path& path,
                   juce::Rectangle<int> area,
                   juce::Colour colour,
                   float edgeAlpha,
                   float centreAlpha)
{
    juce::ColourGradient gradient (
        colour.withAlpha (edgeAlpha), 0.0f, static_cast<float> (area.getY()),
        colour.withAlpha (edgeAlpha), 0.0f, static_cast<float> (area.getBottom()), false);
    gradient.addColour (0.5, colour.withAlpha (centreAlpha));
    g.setGradientFill (gradient);
    g.fillPath (path);
}

template <typename Extent>
void featheredBody (juce::Graphics& g,
                    const std::vector<PaintedPoint>& points,
                    float centreY,
                    Extent extent,
                    juce::Colour colour,
                    float density)
{
    constexpr int layers = 12;
    for (int layer = 0; layer < layers; ++layer)
    {
        const auto progress = static_cast<float> (layer) / static_cast<float> (layers - 1);
        const auto scale = 1.18f - progress * 0.36f;
        const auto alpha = density * (0.018f + progress * 0.034f);
        const auto body = symmetricBody (
            points, centreY,
            [&] (const PaintedPoint& point) { return extent (point) * scale; });
        g.setColour (colour.withAlpha (alpha));
        g.fillPath (body);
    }
}

void drawLayeredFeatures (juce::Graphics& g,
                          const std::vector<PaintedPoint>& points,
                          juce::Rectangle<int> area)
{
    const auto centreY = static_cast<float> (area.getCentreY());
    featheredBody (
        g, points, centreY,
        [] (const PaintedPoint& point)
        {
            const auto amount = std::sqrt (point.tint.transient);
            return (point.height + 7.0f * amount) * amount;
        },
        transientColour, 1.78f);
    featheredBody (
        g, points, centreY,
        [] (const PaintedPoint& point)
        {
            const auto amount = std::sqrt (point.tint.brightness);
            return point.height * (0.70f + 0.30f * amount) * amount;
        },
        brightnessColour, 1.74f);
    featheredBody (
        g, points, centreY,
        [] (const PaintedPoint& point)
        {
            const auto amount = std::sqrt (point.tint.texture);
            const auto motion = std::sin (point.x * 0.027f + 0.9f)
                              + 0.42f * std::sin (point.x * 0.071f + 1.44f);
            return point.height * (0.40f + 0.28f * amount + 0.018f * motion) * amount;
        },
        textureColour, 2.08f);
    featheredBody (
        g, points, centreY,
        [] (const PaintedPoint& point)
        {
            const auto amount = std::sqrt (point.tint.strength);
            return point.height * (0.20f + 0.24f * amount) * amount;
        },
        strengthColour, 1.72f);
}

void drawContinuousBase (juce::Graphics& g,
                         const std::vector<PaintedPoint>& points,
                         juce::Rectangle<int> area,
                         float alpha)
{
    const auto centreY = static_cast<float> (area.getCentreY());
    const auto body = symmetricBody (
        points, centreY, [] (const PaintedPoint& point) { return point.height; });
    gradientFill (g, body, area, waveformColour, alpha * 0.08f, alpha * 0.18f);
    juce::Path upper;
    juce::Path lower;
    if (! points.empty())
    {
        appendSmoothContour (upper, points, centreY,
                             [] (const PaintedPoint& point) { return -point.height; },
                             false, true);
        appendSmoothContour (lower, points, centreY,
                             [] (const PaintedPoint& point) { return point.height; },
                             false, true);
    }
    for (const auto stroke : { 5.4f, 3.0f, 1.2f })
    {
        const auto strokeAlpha = stroke > 5.0f ? 0.04f : stroke > 2.0f ? 0.07f : 0.12f;
        g.setColour (waveformColour.withAlpha (alpha * strokeAlpha));
        g.strokePath (upper, juce::PathStrokeType (stroke));
        g.strokePath (lower, juce::PathStrokeType (stroke));
    }
}
}

void drawWaveform (juce::Graphics& g,
                   const KirinAttackWaveformBatch& batch,
                   const KirinAttackDetailBatch& details,
                   juce::Rectangle<int> area,
                   std::int64_t first,
                   std::int64_t latest,
                   std::uint32_t rate,
                   WaveformStyle style,
                   bool colourAbsoluteFeatures,
                   float alpha)
{
    const auto points = collectPoints (
        batch, area, first, latest, rate,
        [&] (std::int64_t sample)
        {
            return colourAbsoluteFeatures ? absoluteTint (details, sample, rate) : FeatureTint {};
        });
    if (style == WaveformStyle::pulse)
    {
        const auto centreY = static_cast<float> (area.getCentreY());
        g.setColour (waveformColour.withAlpha (alpha * 0.92f));
        for (std::size_t index = 0; index < points.size(); index += 4)
        {
            const auto& point = points[index];
            g.fillEllipse (point.x - 1.25f, centreY - point.height - 1.25f, 2.5f, 2.5f);
            g.fillEllipse (point.x - 1.25f, centreY + point.height - 1.25f, 2.5f, 2.5f);
        }
        return;
    }
    drawContinuousBase (g, points, area, alpha);
    if (colourAbsoluteFeatures)
        drawLayeredFeatures (g, points, area);
}

void drawWaveformDifferences (juce::Graphics& g,
                              const KirinAttackWaveformBatch& postWaveform,
                              const KirinAttackDetailBatch& preDetails,
                              const KirinAttackDetailBatch& postDetails,
                              const KirinAttackPairEventBatch& pairs,
                              juce::Rectangle<int> area,
                              std::int64_t first,
                              std::int64_t latest,
                              std::uint32_t rate)
{
    const auto points = collectPoints (
        postWaveform, area, first, latest, rate,
        [&] (std::int64_t sample)
        {
            return differenceTint (preDetails, postDetails, pairs, sample, rate);
        });
    drawLayeredFeatures (g, points, area);
}

void drawMetricCard (juce::Graphics& g,
                     juce::Rectangle<int> area,
                     const juce::String& title,
                     const juce::String& value,
                     const juce::String& detail,
                     juce::Colour colour,
                     bool active)
{
    area = area.reduced (1, 1);
    if (area.isEmpty())
        return;
    g.setColour (panelColour.withAlpha (0.92f));
    g.fillRoundedRectangle (area.toFloat(), 2.5f);
    g.setColour ((active ? colour : COL_MUTED).withAlpha (active ? 0.95f : 0.38f));
    g.fillRect (area.removeFromTop (2));
    if (area.getHeight() < 28)
    {
        g.setFont (monoFont (7.0f));
        g.setColour (active ? colour : COL_MUTED);
        g.drawText (title + "  " + value, area.reduced (3, 0),
                    juce::Justification::centredLeft);
        return;
    }
    g.setFont (monoFont (7.2f));
    g.setColour (COL_MUTED);
    g.drawText (title, area.removeFromTop (11).reduced (4, 0),
                juce::Justification::centredLeft);
    g.setFont (monoFont (9.0f));
    g.setColour (active ? colour : COL_MUTED);
    g.drawText (value, area.removeFromTop (13).reduced (4, 0),
                juce::Justification::centredLeft);
    g.setFont (monoFont (6.8f));
    g.setColour (COL_MUTED);
    g.drawText (detail, area.reduced (4, 0), juce::Justification::centredLeft);
}
}
