#include "HyphaAttackOrganismPainter.h"

#include <cmath>
#include <vector>

#include "HyphaAttackUiContract.h"

namespace hypha::attack_organism
{
namespace
{
const auto waveformColour = juce::Colour (attack_ui::waveformColour);
const auto strengthColour = juce::Colour (attack_ui::strengthColour);
const auto brightnessColour = juce::Colour (attack_ui::brightnessColour);
const auto transientColour = juce::Colour (attack_ui::transientColour);
const auto textureColour = juce::Colour (attack_ui::textureColour);

struct FeatureTint
{
    float strength = 0.0f;
    float brightness = 0.0f;
    float transient = 0.0f;
    float texture = 0.0f;
};
struct Point { float x = 0.0f; float height = 0.0f; };
struct Band { float inner = 0.0f; float outer = 0.0f; };

float levelHeight (float amplitude, float halfHeight)
{
    const auto db = amplitude > 0.0f ? 20.0f * std::log10 (amplitude)
                                     : attack_ui::absoluteFloorDb;
    const auto normalized = juce::jlimit (
        0.0f, 1.0f, (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb);
    return normalized * juce::jmax (0.0f, halfHeight - 1.0f);
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

FeatureTint differenceTint (const KirinAttackDetail& pre,
                            const KirinAttackDetail& post) noexcept
{
    const auto brightnessAvailable = pre.sharpness_available != 0
                                  && post.sharpness_available != 0;
    return {
        glowAmount (std::abs (post.attack_rms_dbfs - pre.attack_rms_dbfs),
                    attack_ui::strengthDifferenceGlowOnDb,
                    attack_ui::strengthDifferenceGlowFullDb),
        brightnessAvailable
            ? glowAmount (std::abs (post.sharpness_acum - pre.sharpness_acum),
                          attack_ui::brightnessDifferenceGlowOnAcum,
                          attack_ui::brightnessDifferenceGlowFullAcum) : 0.0f,
        glowAmount (std::abs (post.contrast_db - pre.contrast_db),
                    attack_ui::transientDifferenceGlowOnDb,
                    attack_ui::transientDifferenceGlowFullDb),
        glowAmount (std::abs (textureAmount (post) - textureAmount (pre)),
                    attack_ui::textureDifferenceGlowOn,
                    attack_ui::textureDifferenceGlowFull)
    };
}

std::vector<Point> collectShape (const KirinAttackDetail& detail,
                                 juce::Rectangle<int> area,
                                 std::int64_t first,
                                 std::int64_t latest,
                                 std::uint32_t rate,
                                 bool focus)
{
    std::vector<Point> points;
    const auto count = juce::jmin (
        detail.shape_count, static_cast<std::uint32_t> (KIRIN_ATTACK_SHAPE_CAPACITY));
    if (count < 2 || detail.shape_end_sample <= detail.shape_start_sample
        || (! focus && detail.sample_rate != rate))
        return points;
    points.reserve (count);
    const auto halfHeight = area.getHeight() * 0.5f - 2.0f;
    const auto span = detail.shape_end_sample - detail.shape_start_sample;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto amplitude = detail.shape[index];
        if (! std::isfinite (amplitude) || amplitude < 0.0f)
            continue;
        const auto sample = detail.shape_start_sample
                          + static_cast<std::int64_t> (index) * span / (count - 1);
        const auto localX = focus
            ? static_cast<int> (static_cast<std::int64_t> (index) * (area.getWidth() - 1)
                                / (count - 1))
            : attack_ui::sampleX (sample, first, latest, area.getWidth());
        if (localX < 0)
            continue;
        const auto xInteger = area.getX() + localX;
        const auto height = levelHeight (amplitude, halfHeight);
        if (! points.empty() && static_cast<int> (std::lround (points.back().x)) == xInteger)
            points.back().height = juce::jmax (points.back().height, height);
        else
            points.push_back ({ static_cast<float> (xInteger), height });
    }
    return points;
}

template <typename Extent>
void appendContour (juce::Path& path, const std::vector<Point>& points,
                    float centreY, Extent extent, bool reverse, bool startNew)
{
    if (points.empty())
        return;
    const auto pointAt = [&] (std::size_t index) -> const Point&
    {
        return reverse ? points[points.size() - 1 - index] : points[index];
    };
    const auto yAt = [&] (const Point& point) { return centreY + extent (point); };
    const auto& first = pointAt (0);
    if (startNew) path.startNewSubPath (first.x, yAt (first));
    else path.lineTo (first.x, yAt (first));
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
juce::Path body (const std::vector<Point>& points, float centreY, Extent extent)
{
    juce::Path path;
    if (points.empty()) return path;
    appendContour (path, points, centreY,
                   [&] (const Point& point) { return -extent (point); }, false, true);
    appendContour (path, points, centreY, extent, true, false);
    path.closeSubPath();
    return path;
}

template <typename Provider>
void featheredBand (juce::Graphics& g, const std::vector<Point>& points, float centreY,
                    Provider provider, juce::Colour colour, float density)
{
    constexpr int layers = 8;
    for (int layer = 0; layer < layers; ++layer)
    {
        const auto progress = static_cast<float> (layer) / static_cast<float> (layers - 1);
        const auto spread = attack_ui::organismFeather * (1.0f - progress);
        auto band = body (points, centreY, [&] (const Point& point)
        {
            const auto radial = provider (point);
            return radial.outer + (radial.outer - radial.inner) * spread;
        });
        band.setUsingNonZeroWinding (false);
        band.addPath (body (points, centreY, [&] (const Point& point)
        {
            const auto radial = provider (point);
            return juce::jmax (0.0f, radial.inner
                - (radial.outer - radial.inner) * spread);
        }));
        g.setColour (colour.withAlpha (density * (0.020f + progress * 0.035f)));
        g.fillPath (band);
    }
}

Band featureBand (const Point& point, float radius, float halfWidth,
                  float amount, float reach = 0.0f)
{
    const auto eased = std::sqrt (juce::jlimit (0.0f, 1.0f, amount));
    const auto centre = point.height * radius + reach * eased;
    const auto width = (point.height * halfWidth + 0.35f) * eased;
    return { juce::jmax (0.0f, centre - width), centre + width };
}

void featheredBody (juce::Graphics& g, const std::vector<Point>& points,
                    float centreY, float amount, float bodyRadius,
                    juce::Colour colour, float density)
{
    constexpr int layers = 8;
    for (int layer = 0; layer < layers; ++layer)
    {
        const auto progress = static_cast<float> (layer) / static_cast<float> (layers - 1);
        const auto scale = 1.12f - progress * 0.24f;
        const auto radius = bodyRadius
                          * std::sqrt (juce::jlimit (0.0f, 1.0f, amount)) * scale;
        g.setColour (colour.withAlpha (density * (0.020f + progress * 0.035f)));
        g.fillPath (body (points, centreY,
                          [radius] (const Point& point) { return point.height * radius; }));
    }
}

void drawOrganism (juce::Graphics& g, const std::vector<Point>& points,
                   juce::Rectangle<int> area, FeatureTint tint, bool fullDetail)
{
    if (points.size() < 2)
        return;
    const auto centreY = static_cast<float> (area.getCentreY());
    featheredBand (g, points, centreY, [tint] (const Point& point)
    {
        return featureBand (point, attack_ui::transientAuraRadius,
                            attack_ui::transientAuraHalfWidth, tint.transient,
                            attack_ui::transientAuraReach);
    }, transientColour, 1.72f);
    if (fullDetail)
    {
        featheredBand (g, points, centreY, [tint] (const Point& point)
        {
            return featureBand (point, attack_ui::brightnessShellRadius,
                                attack_ui::brightnessShellHalfWidth, tint.brightness);
        }, brightnessColour, 1.58f);
        featheredBody (g, points, centreY, tint.texture,
                       attack_ui::textureBodyRadius, textureColour, 1.48f);
    }
    featheredBody (g, points, centreY, tint.strength,
                   attack_ui::strengthCoreRadius, strengthColour, 1.65f);
}

void drawShapeBody (juce::Graphics& g, const std::vector<Point>& points,
                    juce::Rectangle<int> area, float alpha, bool fill)
{
    const auto centreY = static_cast<float> (area.getCentreY());
    const auto shape = body (points, centreY, [] (const Point& point) { return point.height; });
    if (fill)
    {
        juce::ColourGradient gradient (
            waveformColour.withAlpha (alpha * 0.035f), 0.0f,
            static_cast<float> (area.getY()),
            waveformColour.withAlpha (alpha * 0.035f), 0.0f,
            static_cast<float> (area.getBottom()), false);
        gradient.addColour (0.5, waveformColour.withAlpha (alpha * 0.11f));
        g.setGradientFill (gradient);
        g.fillPath (shape);
    }
    for (const auto sign : { -1.0f, 1.0f })
    {
        juce::Path contour;
        appendContour (contour, points, centreY,
                       [sign] (const Point& point) { return sign * point.height; }, false, true);
        g.setColour (waveformColour.withAlpha (alpha * 0.12f));
        g.strokePath (contour, juce::PathStrokeType (4.0f));
        g.setColour (waveformColour.withAlpha (alpha * 0.68f));
        g.strokePath (contour, juce::PathStrokeType (1.0f));
    }
}
}

void drawAbsoluteOverview (juce::Graphics& g, const KirinAttackDetailBatch& details,
                           juce::Rectangle<int> area, std::int64_t first,
                           std::int64_t latest, std::uint32_t rate)
{
    const auto count = juce::jmin (
        details.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        drawOrganism (g, collectShape (details.details[index], area, first, latest, rate, false),
                      area, absoluteTint (details.details[index]), false);
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
        if (pre != nullptr && post != nullptr)
            drawOrganism (g, collectShape (*post, area, first, latest, rate, false), area,
                          differenceTint (*pre, *post), false);
    }
}

void drawFocus (juce::Graphics& g, const KirinAttackDetail* pre,
                const KirinAttackDetail* post, juce::Rectangle<int> area)
{
    if (post == nullptr || area.getWidth() < 2 || area.getHeight() < 2)
        return;
    const auto postPoints = collectShape (*post, area, 0, 1, post->sample_rate, true);
    drawShapeBody (g, postPoints, area, 0.96f, true);
    if (pre != nullptr)
        drawShapeBody (g, collectShape (*pre, area, 0, 1, pre->sample_rate, true),
                       area, 0.62f, false);
    drawOrganism (g, postPoints, area, pre != nullptr ? differenceTint (*pre, *post)
                                                       : absoluteTint (*post), true);
}
}
