#include "HyphaAttackOrganismPainter.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "HyphaAttackSpecimenPainter.h"
#include "HyphaAttackUiContract.h"

namespace hypha::attack_organism
{
namespace
{
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

struct Point
{
    float x = 0.0f;
    float height = 0.0f;
    float phase = 0.0f;
};

struct Band { float inner = 0.0f; float outer = 0.0f; };

float shapeHeight (float amplitude, float halfHeight)
{
    if (! std::isfinite (amplitude) || amplitude <= 0.0f)
        return 0.0f;
    const auto perceptual = std::pow (juce::jlimit (0.0f, 1.0f, amplitude), 0.52f);
    return perceptual * juce::jmax (0.0f, halfHeight - 1.0f);
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
        const auto sample = detail.shape_start_sample
                          + static_cast<std::int64_t> (index) * span / (count - 1);
        const auto localX = focus
            ? static_cast<int> (static_cast<std::int64_t> (index) * (area.getWidth() - 1)
                                / (count - 1))
            : attack_ui::sampleX (sample, first, latest, area.getWidth());
        if (localX < 0)
            continue;
        const auto phase = static_cast<float> (index) / static_cast<float> (count - 1);
        float weightedAmplitude = 0.0f;
        float weightSum = 0.0f;
        for (int offset = -2; offset <= 2; ++offset)
        {
            const auto source = static_cast<std::uint32_t> (juce::jlimit (
                0, static_cast<int> (count - 1), static_cast<int> (index) + offset));
            const auto amplitude = detail.shape[source];
            if (! std::isfinite (amplitude) || amplitude < 0.0f)
                continue;
            const auto weight = static_cast<float> (3 - std::abs (offset));
            weightedAmplitude += amplitude * weight;
            weightSum += weight;
        }
        const auto edgeTaper = std::sqrt (juce::jlimit (
            0.0f, 1.0f, juce::jmin (phase, 1.0f - phase) / 0.055f));
        const auto x = area.getX() + localX;
        const auto height = shapeHeight (
            weightSum > 0.0f ? weightedAmplitude / weightSum : 0.0f, halfHeight) * edgeTaper;
        if (! points.empty() && static_cast<int> (std::lround (points.back().x)) == x)
            points.back().height = juce::jmax (points.back().height, height);
        else
            points.push_back ({ static_cast<float> (x), height, phase });
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
    for (std::size_t index = 1; index < points.size(); ++index)
    {
        const auto& previous = pointAt (index - 1);
        const auto& point = pointAt (index);
        path.quadraticTo (previous.x, yAt (previous),
                          (previous.x + point.x) * 0.5f,
                          (yAt (previous) + yAt (point)) * 0.5f);
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
                   [&] (const Point& point) { return -extent (point) * 0.82f; }, false, true);
    appendContour (path, points, centreY,
                   [&] (const Point& point) { return extent (point) * 1.06f; }, true, false);
    path.closeSubPath();
    return path;
}

template <typename Provider>
void featheredBand (juce::Graphics& g, const std::vector<Point>& points, float centreY,
                    Provider provider, juce::Colour colour, float density)
{
    constexpr int layers = 7;
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
        g.setColour (colour.withAlpha (density * (0.018f + progress * 0.044f)));
        g.fillPath (band);
    }
}

Band featureBand (const Point& point, float radius, float halfWidth,
                  float amount, float reach = 0.0f)
{
    const auto eased = std::sqrt (juce::jlimit (0.0f, 1.0f, amount));
    const auto decayReach = reach * eased * point.phase * point.phase;
    const auto centre = point.height * radius + decayReach;
    const auto width = (point.height * halfWidth + 0.28f) * eased;
    return { juce::jmax (0.0f, centre - width), centre + width };
}

void featheredBody (juce::Graphics& g, const std::vector<Point>& points,
                    float centreY, float amount, float radius,
                    juce::Colour colour, float density)
{
    constexpr int layers = 7;
    for (int layer = 0; layer < layers; ++layer)
    {
        const auto progress = static_cast<float> (layer) / static_cast<float> (layers - 1);
        const auto scale = (1.14f - progress * 0.25f)
                         * std::sqrt (juce::jlimit (0.0f, 1.0f, amount));
        g.setColour (colour.withAlpha (density * (0.018f + progress * 0.047f)));
        g.fillPath (body (points, centreY, [=] (const Point& point)
        {
            return point.height * radius * scale;
        }));
    }
}

void drawTextureFibres (juce::Graphics& g, const std::vector<Point>& points,
                        float centreY, float amount)
{
    const auto visible = juce::jlimit (0.0f, 1.0f, amount);
    if (visible <= 0.0f)
        return;
    constexpr int fibres = 11;
    for (int index = 1; index <= fibres; ++index)
    {
        const auto fraction = static_cast<float> (index) / static_cast<float> (fibres + 1);
        const auto radial = (0.12f + fraction * 0.50f) * std::sqrt (visible);
        for (const auto sign : { -1.0f, 1.0f })
        {
            juce::Path fibre;
            appendContour (fibre, points, centreY, [=] (const Point& point)
            {
                const auto meander = std::sin ((point.phase * 3.0f + fraction)
                                                * juce::MathConstants<float>::pi)
                                    * point.height * 0.025f * visible;
                return sign * (point.height * radial + meander);
            }, false, true);
            g.setColour (textureColour.withAlpha (0.045f + visible * 0.13f));
            g.strokePath (fibre, juce::PathStrokeType (index % 3 == 0 ? 0.78f : 0.48f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }
    }
}

void drawNucleus (juce::Graphics& g, const std::vector<Point>& points,
                  float centreY, float strength)
{
    if (points.empty() || strength <= 0.0f)
        return;
    const auto peak = std::max_element (points.begin(), points.end(), [] (const Point& left,
                                                                          const Point& right)
    {
        return left.height < right.height;
    });
    const auto amount = std::sqrt (juce::jlimit (0.0f, 1.0f, strength));
    const auto radiusY = juce::jmax (1.5f, peak->height * 0.31f * amount);
    const auto radiusX = juce::jmax (2.5f, juce::jmin (20.0f, radiusY * 1.65f));
    const auto makeLens = [&] (float scale)
    {
        const auto x = peak->x;
        const auto y = centreY - radiusY * 0.05f;
        const auto rx = radiusX * scale;
        const auto ry = radiusY * scale;
        juce::Path lens;
        lens.startNewSubPath (x - rx * 0.82f, y);
        lens.cubicTo (x - rx * 0.55f, y - ry,
                      x + rx * 0.32f, y - ry * 0.88f,
                      x + rx, y + ry * 0.05f);
        lens.cubicTo (x + rx * 0.30f, y + ry,
                      x - rx * 0.62f, y + ry * 0.78f,
                      x - rx * 0.82f, y);
        lens.closeSubPath();
        return lens;
    };
    for (int layer = 0; layer < 7; ++layer)
    {
        const auto progress = static_cast<float> (layer) / 6.0f;
        g.setColour (strengthColour.withAlpha (0.025f + progress * 0.075f));
        g.fillPath (makeLens (1.18f - progress * 0.62f));
    }
    g.setColour (juce::Colour (0xffffedb0).withAlpha (0.72f * amount));
    g.fillPath (makeLens (0.16f));
}

void paintOrganism (juce::Graphics& g, const std::vector<Point>& points,
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
    }, transientColour, fullDetail ? 1.85f : 1.22f);
    if (fullDetail)
    {
        featheredBand (g, points, centreY, [tint] (const Point& point)
        {
            return featureBand (point, attack_ui::brightnessShellRadius,
                                attack_ui::brightnessShellHalfWidth, tint.brightness);
        }, brightnessColour, 1.72f);
        featheredBody (g, points, centreY, tint.texture,
                       attack_ui::textureBodyRadius, textureColour, 1.34f);
        drawTextureFibres (g, points, centreY, tint.texture);
    }
    else
    {
        featheredBand (g, points, centreY, [tint] (const Point& point)
        {
            return featureBand (point, attack_ui::brightnessShellRadius,
                                attack_ui::brightnessShellHalfWidth, tint.brightness);
        }, brightnessColour, 0.72f);
        featheredBody (g, points, centreY, tint.texture,
                       attack_ui::textureBodyRadius, textureColour, 0.62f);
    }
    featheredBody (g, points, centreY, tint.strength,
                   attack_ui::strengthCoreRadius, strengthColour, fullDetail ? 2.0f : 1.50f);
    if (fullDetail)
        drawNucleus (g, points, centreY, tint.strength);
}

}

void drawAbsoluteOverview (juce::Graphics& g, const KirinAttackDetailBatch& details,
                           juce::Rectangle<int> area, std::int64_t first,
                           std::int64_t latest, std::uint32_t rate)
{
    const auto count = juce::jmin (
        details.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        paintOrganism (g, collectShape (details.details[index], area, first, latest, rate, false),
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
            paintOrganism (g, collectShape (*post, area, first, latest, rate, false),
                           area, differenceTint (*pre, *post), false);
    }
}

void drawFocus (juce::Graphics& g, const KirinAttackDetail* pre,
                const KirinAttackDetail* post, juce::Rectangle<int> area)
{
    if (post == nullptr || area.getWidth() < 2 || area.getHeight() < 2)
        return;
    const auto tint = pre != nullptr ? differenceTint (*pre, *post) : absoluteTint (*post);
    attack_specimen::draw (g, *post, area,
                           { tint.strength, tint.brightness, tint.transient, tint.texture });
}
}
