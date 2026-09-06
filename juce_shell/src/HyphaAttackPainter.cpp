#include "HyphaAttackPainter.h"
#include "HyphaAttackOrganismPainter.h"
#include "HyphaAttackUiContract.h"
#include "HyphaPolylineGeometry.h"
#include <array>
#include <cmath>

namespace hypha::attack_painter
{
namespace
{
constexpr std::size_t capacity = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
struct Flow
{
    std::array<float, capacity> x {}, height {};
    std::array<bool, capacity> start {};
    std::size_t count = 0;
};
Flow collect (const KirinAttackWaveformBatch& batch, juce::Rectangle<int> area,
              std::int64_t first, std::int64_t latest, std::uint32_t rate)
{
    Flow flow;
    std::int64_t previousEnd = 0;
    bool continuous = false;
    const auto count = juce::jmin (batch.count, static_cast<std::uint32_t> (capacity));
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto& p = batch.points[i];
        const auto duration = static_cast<long double> (p.end_sample) - p.start_sample;
        if (p.sample_rate != rate || ! std::isfinite (p.rms_dbfs)
            || duration <= 0 || duration > rate)
        { continuous = false; continue; }
        const auto sample = p.start_sample + static_cast<std::int64_t> (duration / 2);
        const auto x = attack_ui::sampleX (sample, first, latest, area.getWidth());
        if (x < 0) { continuous = false; continue; }
        const auto at = flow.count++;
        flow.x[at] = static_cast<float> (area.getX() + x);
        const auto energy = juce::jlimit (0.0f, 1.0f,
            (p.rms_dbfs - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb);
        flow.height[at] = std::pow (energy, 0.64f) * juce::jmax (0.0f, area.getHeight() * 0.5f - 3);
        flow.start[at] = ! continuous || p.start_sample != previousEnd;
        continuous = true; previousEnd = p.end_sample;
    }
    return flow;
}

std::array<float, capacity> positions (const Flow& flow, juce::Rectangle<int> area, int strand)
{
    std::array<float, capacity> y {};
    constexpr std::array<float, 5> radial { -.80f, -.42f, -.03f, .36f, .76f };
    for (std::size_t i = 0; i < flow.count; ++i)
    {
        const auto phase = (flow.x[i] - area.getX()) / juce::jmax (1.0f, static_cast<float> (area.getWidth()));
        const auto curl = std::sin (phase * (5.3f + strand * .51f) + strand * 1.71f) * .14f
                        + std::sin (phase * 3.1f + .4f) * .09f;
        y[i] = static_cast<float> (area.getCentreY())
             + flow.height[i] * juce::jlimit (-.98f, .98f, radial[static_cast<std::size_t> (strand)] + curl);
    }
    return y;
}

juce::Path contour (const Flow& flow, const std::array<float, capacity>& y)
{
    juce::Path path;
    path.preallocateSpace (static_cast<int> (flow.count * 3));
    for (std::size_t first = 0; first < flow.count;)
    {
        auto last = first;
        while (last + 1 < flow.count && ! flow.start[last + 1]) ++last;
        const auto keep = polyline_geometry::retainedVertices (flow.x, y, first, last, 0.05);
        path.startNewSubPath (flow.x[first], y[first]);
        for (auto i = first + 1; i <= last; ++i)
            if (keep[i]) path.lineTo (flow.x[i], y[i]);
        first = last + 1;
    }
    return path;
}

void drawFlow (juce::Graphics& g, const Flow& flow, juce::Rectangle<int> area,
               float alpha, bool reference)
{
    if (flow.count < 2) return;
    const auto colour = juce::Colour (attack_ui::waveformColour);
    juce::ColourGradient gradient (colour.withAlpha (alpha * .035f),
        static_cast<float> (area.getX()), static_cast<float> (area.getY()),
        colour.withAlpha (alpha * .035f), static_cast<float> (area.getX()),
        static_cast<float> (area.getBottom()), false);
    gradient.addColour (.45, colour.withAlpha (alpha * (reference ? .46f : .65f)));
    gradient.addColour (.58, colour.withAlpha (alpha * (reference ? .38f : .54f)));
    g.setGradientFill (gradient);
    // Bounded continuous depth: no per-threshold re-stroking, raster blur or resampling.
    // Geometry simplification is <= 0.05 logical pixels; the 10 ms observations are untouched.
    juce::Path fibres;
    for (int strand = 0; strand < 5; ++strand)
    {
        if (reference && strand % 2 != 0) continue;
        const auto path = contour (flow, positions (flow, area, strand));
        fibres.addPath (path);
    }
    // One raster pass for all fibres, with separate subpaths preserving source gaps.
    // Bevels on subpixel envelope fibres avoid arc tessellation at every 10 ms vertex.
    // The focus fan retains curved joints and rounded ends.
    g.strokePath (fibres, juce::PathStrokeType (reference ? .58f : .70f, juce::PathStrokeType::beveled));
}
}

void drawWaveform (juce::Graphics& g, const KirinAttackWaveformBatch& batch,
                   const KirinAttackDetailBatch& details, juce::Rectangle<int> area,
                   std::int64_t first, std::int64_t latest, std::uint32_t rate,
                   WaveformStyle style, bool colourAbsoluteFeatures, float alpha,
                   attack_overview_glyph::Cache* cache)
{
    const auto points = collect (batch, area, first, latest, rate);
    drawFlow (g, points, area, alpha, style == WaveformStyle::trace);
    if (style != WaveformStyle::trace && colourAbsoluteFeatures)
        attack_organism::drawAbsoluteOverview (g, details, area, first, latest, rate, cache);
}
void drawWaveformDifferences (juce::Graphics& g, const KirinAttackDetailBatch& pre,
                              const KirinAttackDetailBatch& post, const KirinAttackPairEventBatch& pairs,
                              juce::Rectangle<int> area, std::int64_t first,
                              std::int64_t latest, std::uint32_t rate, attack_overview_glyph::Cache* cache)
{
    attack_organism::drawDifferenceOverview (g, pre, post, pairs, area, first, latest, rate, cache);
}
void drawEventFocus (juce::Graphics& g, const KirinAttackDetail* pre,
                     const KirinAttackDetail* post, juce::Rectangle<int> area,
                     const attack_fan::Motion& motion, attack_overview_glyph::Cache* cache)
{
    attack_organism::drawFocus (g, pre, post, area, motion, cache);
}
}
