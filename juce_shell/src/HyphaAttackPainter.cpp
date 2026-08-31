#include "HyphaAttackPainter.h"

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
};

float levelHeight (float db, float halfHeight)
{
    const auto normalized = juce::jlimit (
        0.0f, 1.0f, (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb);
    return normalized * juce::jmax (0.0f, halfHeight - 1.0f);
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
            points.push_back ({ static_cast<float> (area.getX() + localX),
                                levelHeight (point.rms_dbfs, halfHeight) });
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
    const auto points = collectPoints (batch, area, first, latest, rate);
    if (style == WaveformStyle::trace)
    {
        const auto centreY = static_cast<float> (area.getCentreY());
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
        g.setColour (waveformColour.withAlpha (alpha * 0.12f));
        g.strokePath (upper, juce::PathStrokeType (4.4f));
        g.strokePath (lower, juce::PathStrokeType (4.4f));
        g.setColour (waveformColour.withAlpha (alpha * 0.72f));
        g.strokePath (upper, juce::PathStrokeType (1.0f));
        g.strokePath (lower, juce::PathStrokeType (1.0f));
        return;
    }
    drawContinuousBase (g, points, area, alpha);
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
