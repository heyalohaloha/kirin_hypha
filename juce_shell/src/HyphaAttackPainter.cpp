#include "HyphaAttackPainter.h"

#include <cmath>
#include <vector>

#include "HyphaAttackOrganismPainter.h"
#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha::attack_painter
{
namespace
{
const auto waveformColour = juce::Colour (attack_ui::waveformColour);

struct PaintedPoint
{
    float x = 0.0f;
    float height = 0.0f;
    float energy = 0.0f;
};

float levelAmount (float db)
{
    return juce::jlimit (
        0.0f, 1.0f, (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb);
}

std::vector<PaintedPoint> collectPoints (const KirinAttackWaveformBatch& batch,
                                         juce::Rectangle<int> area,
                                         std::int64_t first,
                                         std::int64_t latest,
                                         std::uint32_t rate)
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
        {
            const auto energy = levelAmount (point.rms_dbfs);
            points.push_back ({ static_cast<float> (area.getX() + localX),
                                std::pow (energy, 0.64f)
                                    * juce::jmax (0.0f, halfHeight - 1.0f),
                                energy });
        }
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

struct Strand
{
    float radial = 0.0f;
    float meander = 0.0f;
    float frequency = 0.0f;
    float phase = 0.0f;
    float opacity = 0.0f;
    float width = 0.0f;
};

constexpr int strandCount = 19;

Strand strandForIndex (int index)
{
    const auto order = static_cast<float> (index) + 0.5f;
    const auto fraction = order / static_cast<float> (strandCount);
    const auto irregular = std::sin (order * 2.17f) * 0.035f;
    const auto cycle = std::fmod (order * 0.6180339887f, 1.0f);
    const auto pulse = 0.5f + 0.5f * std::sin (order * 1.71f);
    return {
        -0.92f + fraction * 1.84f + irregular,
        (0.34f + pulse * 0.33f) * (0.82f + 0.18f * std::sin (fraction * 3.1f)),
        0.74f + cycle * 2.26f,
        std::fmod (order * 0.3819660113f, 1.0f),
        0.065f + pulse * 0.085f + (index % 5 == 0 ? 0.055f : 0.0f),
        0.34f + pulse * 0.27f
    };
}

float strandOffset (const PaintedPoint& point,
                    juce::Rectangle<int> area,
                    const Strand& strand)
{
    const auto xPhase = (point.x - static_cast<float> (area.getX()))
                      / static_cast<float> (juce::jmax (1, area.getWidth()));
    const auto primary = std::sin ((xPhase * strand.frequency + strand.phase)
                                   * juce::MathConstants<float>::twoPi);
    const auto secondary = std::sin ((xPhase * (strand.frequency * 0.47f + 0.31f)
                                      + strand.phase * 1.71f)
                                     * juce::MathConstants<float>::pi);
    const auto livingAxis = std::sin ((xPhase * 0.83f + 0.19f)
                                      * juce::MathConstants<float>::twoPi) * 0.19f;
    const auto radial = livingAxis + strand.radial
                      + strand.meander * (primary * 0.68f + secondary * 0.32f);
    return point.height * juce::jlimit (-1.06f, 1.06f, radial);
}

juce::Path strandPath (const std::vector<PaintedPoint>& points,
                       juce::Rectangle<int> area,
                       float centreY,
                       const Strand& strand)
{
    juce::Path path;
    appendSmoothContour (path, points, centreY, [&] (const PaintedPoint& point)
    {
        return strandOffset (point, area, strand);
    }, false, true);
    return path;
}

juce::Path activeStrandPath (const std::vector<PaintedPoint>& points,
                             juce::Rectangle<int> area,
                             float centreY,
                             const Strand& strand,
                             float threshold)
{
    juce::Path path;
    const auto appendRun = [&] (std::size_t start, std::size_t end)
    {
        if (end - start < 2)
            return;
        const auto yAt = [&] (const PaintedPoint& point)
        {
            return centreY + strandOffset (point, area, strand);
        };
        path.startNewSubPath (points[start].x, yAt (points[start]));
        for (auto index = start + 1; index < end; ++index)
        {
            const auto& previous = points[index - 1];
            const auto& point = points[index];
            path.quadraticTo (previous.x, yAt (previous),
                              (previous.x + point.x) * 0.5f,
                              (yAt (previous) + yAt (point)) * 0.5f);
        }
        path.lineTo (points[end - 1].x, yAt (points[end - 1]));
    };
    auto runStart = points.size();
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        if (points[index].energy >= threshold)
        {
            if (runStart == points.size())
                runStart = index;
            continue;
        }
        if (runStart != points.size())
            appendRun (runStart, index);
        runStart = points.size();
    }
    if (runStart != points.size())
        appendRun (runStart, points.size());
    return path;
}

juce::Path flowVeil (const std::vector<PaintedPoint>& points,
                     juce::Rectangle<int> area,
                     float centreY,
                     const Strand& first,
                     const Strand& second)
{
    juce::Path path;
    appendSmoothContour (path, points, centreY, [&] (const PaintedPoint& point)
    {
        return strandOffset (point, area, first);
    }, false, true);
    appendSmoothContour (path, points, centreY, [&] (const PaintedPoint& point)
    {
        return strandOffset (point, area, second);
    }, true, false);
    path.closeSubPath();
    return path;
}

void drawMeasuredFlow (juce::Graphics& g,
                       const std::vector<PaintedPoint>& points,
                       juce::Rectangle<int> area,
                       float alpha,
                       bool referenceOnly)
{
    if (points.size() < 2)
        return;
    const auto centreY = static_cast<float> (area.getCentreY());
    if (! referenceOnly)
    {
        constexpr int veilPairs[][2] {{ 0, 4 }, { 4, 9 }, { 9, 14 }, { 14, 18 }};
        for (const auto& pair : veilPairs)
        {
            auto veil = flowVeil (points, area, centreY,
                                  strandForIndex (pair[0]), strandForIndex (pair[1]));
            veil.setUsingNonZeroWinding (false);
            g.setColour (waveformColour.withAlpha (alpha * 0.006f));
            g.fillPath (veil);
        }
    }
    const Strand spine { 0.0f, 0.055f, 1.21f, 0.37f, 0.0f, 0.0f };
    const auto spinePath = strandPath (points, area, centreY, spine);
    g.setColour (waveformColour.withAlpha (alpha * 0.008f));
    g.strokePath (spinePath, juce::PathStrokeType (referenceOnly ? 2.4f : 4.6f,
                                                   juce::PathStrokeType::curved));
    for (const auto threshold : { 0.18f, 0.28f, 0.52f })
    {
        const auto activeSpine = activeStrandPath (
            points, area, centreY, spine, threshold);
        g.setColour (waveformColour.withAlpha (
            alpha * (threshold < 0.2f ? 0.025f : threshold < 0.4f ? 0.070f : 0.105f)));
        g.strokePath (activeSpine, juce::PathStrokeType (
            referenceOnly ? 0.62f : 0.52f, juce::PathStrokeType::curved));
    }

    for (int index = 0; index < strandCount; ++index)
    {
        if (referenceOnly && index % 3 != 1)
            continue;
        const auto strand = strandForIndex (index);
        const auto path = strandPath (points, area, centreY, strand);
        g.setColour (waveformColour.withAlpha (alpha * 0.006f));
        g.strokePath (path, juce::PathStrokeType (strand.width,
                                                  juce::PathStrokeType::curved));
        if (! referenceOnly && index % 5 == 0)
        {
            const auto glow = activeStrandPath (
                points, area, centreY, strand, 0.26f);
            g.setColour (waveformColour.withAlpha (alpha * 0.032f));
            g.strokePath (glow, juce::PathStrokeType (4.2f,
                                                      juce::PathStrokeType::curved));
        }
        constexpr float thresholds[] { 0.18f, 0.24f, 0.45f, 0.70f };
        constexpr float opacities[] { 0.015f, 0.055f, 0.075f, 0.095f };
        for (int pass = 0; pass < 4; ++pass)
        {
            const auto active = activeStrandPath (
                points, area, centreY, strand, thresholds[pass]);
            const auto opacity = opacities[pass] * (0.66f + strand.opacity * 2.2f)
                               * (referenceOnly ? 1.32f : 1.0f);
            g.setColour (waveformColour.withAlpha (alpha * opacity));
            g.strokePath (active, juce::PathStrokeType (
                referenceOnly ? strand.width * 0.88f : strand.width,
                juce::PathStrokeType::curved));
        }
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
    const auto points = collectPoints (batch, area, first, latest, rate);
    if (style == WaveformStyle::trace)
    {
        drawMeasuredFlow (g, points, area, alpha, true);
        return;
    }
    drawMeasuredFlow (g, points, area, alpha, false);
    if (colourAbsoluteFeatures)
        attack_organism::drawAbsoluteOverview (g, details, area, first, latest, rate);
}

void drawWaveformDifferences (juce::Graphics& g,
                              const KirinAttackDetailBatch& preDetails,
                              const KirinAttackDetailBatch& postDetails,
                              const KirinAttackPairEventBatch& pairs,
                              juce::Rectangle<int> area,
                              std::int64_t first,
                              std::int64_t latest,
                              std::uint32_t rate)
{
    attack_organism::drawDifferenceOverview (
        g, preDetails, postDetails, pairs, area, first, latest, rate);
}

void drawEventFocus (juce::Graphics& g,
                     const KirinAttackDetail* preDetail,
                     const KirinAttackDetail* postDetail,
                     juce::Rectangle<int> area)
{
    attack_organism::drawFocus (g, preDetail, postDetail, area);
}

}
