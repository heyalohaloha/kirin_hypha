#include "HyphaSpectrumTerrain.h"

#include "HyphaAbsoluteSpectrumHistory.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hypha::spectrum_terrain
{
namespace
{
constexpr int crossEvery = 6;         // a grid line along time every sixth column
constexpr float perspective = 2.4f;   // the oldest ridge is 1 / (1 + 2.4) of the front size

float perspectiveScale (float depth) noexcept
{
    return 1.0f / (1.0f + perspective * depth);
}

struct Ridge
{
    bool valid = false;
    float depth = 0.0f;
    std::array<juce::Point<float>, columnCount> points {};
};

void draw (juce::Graphics& g, juce::Rectangle<float> plot, const Source& source,
           const Scale& scale, juce::Colour ink, juce::Colour floor, double seconds)
{
    // Each ridge takes the measured frame nearest to its age. A ridge with no frame within half a
    // ridge spacing is left out, so a gap in the measurement stays empty instead of being bridged.
    const auto spacing = seconds / (double) (ridgeCount - 1);
    std::array<Ridge, ridgeCount> ridges {};
    size_t cursor = 0u;
    for (int row = 0; row < ridgeCount; ++row)
    {
        const auto target = ridgeAge (row, seconds);
        while (cursor + 1u < source.count && source.ageSeconds (cursor + 1u) >= target)
            ++cursor;
        auto frame = cursor;
        if (cursor + 1u < source.count
            && std::abs (source.ageSeconds (cursor + 1u) - target)
                   < std::abs (source.ageSeconds (cursor) - target))
            frame = cursor + 1u;
        const auto age = source.ageSeconds (frame);
        if (std::abs (age - target) > 0.5 * spacing || age > seconds)
            continue;
        auto& ridge = ridges[(size_t) row];
        ridge.valid = true;
        ridge.depth = (float) juce::jlimit (0.0, 1.0, age / seconds);
        for (int column = 0; column < columnCount; ++column)
        {
            const auto from = (float) column / (float) columnCount;
            const auto to = (float) (column + 1) / (float) columnCount;
            ridge.points[(size_t) column] = project (plot, scale, ridge.depth, 0.5f * (from + to),
                                                     source.peakIn (frame, from, to));
        }
    }

    const auto curtainDepth = (scale.frontZeroY - project (plot, scale, 1.0f, 0.5f, 0.0f).y) * 0.14f;
    for (int row = 0; row < ridgeCount; ++row)
    {
        const auto& ridge = ridges[(size_t) row];
        if (! ridge.valid)
            continue;
        const auto light = 1.0f - 0.78f * ridge.depth; // older ridges are fainter
        const auto& p = ridge.points;
        // The curtain hides the farther ridges behind this one. It reaches only a little below the
        // ridge's own floor, and the newest ridge keeps none: the flat plot below stays as it is.
        if (ridge.depth > 0.03f)
        {
            const auto bottom = std::min (scale.frontZeroY,
                                          project (plot, scale, ridge.depth, 0.5f, 0.0f).y
                                              + curtainDepth);
            juce::Path curtain;
            curtain.startNewSubPath (p.front().x, bottom);
            for (const auto& point : p)
                curtain.lineTo (point.x, std::min (point.y, bottom));
            curtain.lineTo (p.back().x, bottom);
            curtain.closeSubPath();
            g.setColour (floor.withAlpha (0.78f));
            g.fillPath (curtain);
        }
        // Grid lines along time join only neighbouring ridges; a missing ridge breaks them.
        if (row > 0 && ridges[(size_t) row - 1u].valid)
        {
            const auto& previous = ridges[(size_t) row - 1u].points;
            juce::Path cross;
            for (int column = crossEvery / 2; column < columnCount; column += crossEvery)
            {
                cross.startNewSubPath (previous[(size_t) column]);
                cross.lineTo (p[(size_t) column]);
            }
            g.setColour (ink.withAlpha (0.20f * light));
            g.strokePath (cross, juce::PathStrokeType (0.7f));
        }
        juce::Path line;
        line.startNewSubPath (p.front());
        for (size_t column = 1u; column < p.size(); ++column)
            line.lineTo (p[column]);
        g.setColour (ink.withAlpha (0.08f + 0.62f * light));
        g.strokePath (line, juce::PathStrokeType (0.6f + 0.5f * light,
                                                  juce::PathStrokeType::beveled));
    }
}
}

double ridgeAge (int ridge, double seconds) noexcept
{
    return seconds * (1.0 - (double) ridge / (double) (ridgeCount - 1));
}

juce::Point<float> project (juce::Rectangle<float> plot, const Scale& scale, float depth,
                            float normalisedX, float value) noexcept
{
    const auto farthest = perspectiveScale (1.0f);
    const auto s = perspectiveScale (juce::jlimit (0.0f, 1.0f, depth));
    const auto horizonY = plot.getY() + plot.getHeight() * scale.horizon;
    const auto zeroY = horizonY + (scale.frontZeroY - horizonY) * (s - farthest) / (1.0f - farthest);
    const auto clipped = juce::jlimit (scale.minimum, scale.maximum, value);
    const auto centreX = plot.getCentreX();
    return { centreX + (plot.getX() + normalisedX * plot.getWidth() - centreX) * s,
             zeroY - clipped * scale.pixelsPerUnit * s };
}

Scale levelScale (juce::Rectangle<float> plot) noexcept
{
    // Mountains rise from the floor, so the oldest floor sits a third of the way down: the range
    // stays inside the plot at its full height.
    return { plot.getBottom(), plot.getHeight() / -levelFloorDbfs, 0.0f, -levelFloorDbfs, 0.34f };
}

void paint (juce::Graphics& g, juce::Rectangle<float> plot, const Source& source,
            const Scale& scale, juce::Colour ink, juce::Colour floor, double seconds)
{
    if (source.count < 2u || plot.getWidth() < minimumPlotWidth || plot.isEmpty()
        || ! (seconds > 0.0) || ! source.ageSeconds || ! source.peakIn)
        return;
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (plot.toNearestInt());
   #if JUCE_MAC
    // CoreGraphics fills many-vertex paths slowly; the software rasterizer draws the ridges into
    // one device-resolution image, which is then drawn once.
    const auto dpi = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (std::isfinite (dpi) && dpi > 0.0f && dpi <= 4.0f)
    {
        const auto pw = (int) std::ceil (plot.getWidth() * dpi);
        const auto ph = (int) std::ceil (plot.getHeight() * dpi);
        juce::Image raster (juce::Image::ARGB, pw, ph, true, juce::SoftwareImageType {});
        if (raster.isValid())
        {
            {
                juce::Graphics pixels (raster);
                pixels.addTransform (juce::AffineTransform::translation (-plot.getX(), -plot.getY())
                                         .scaled (dpi));
                draw (pixels, plot, source, scale, ink, floor, seconds);
            }
            g.setOpacity (1.0f);
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImage (raster, { plot.getX(), plot.getY(), (float) pw / dpi, (float) ph / dpi });
            return;
        }
    }
   #endif
    draw (g, plot, source, scale, ink, floor, seconds);
}

bool paintLevelLandscape (juce::Graphics& g, juce::Rectangle<float> plot,
                          const absolute_spectrum::History& history)
{
    if (history.size() < 2u || plot.getWidth() < minimumPlotWidth)
        return false;
    const auto& newest = history.at (history.size() - 1u);
    const Source source {
        history.size(),
        [&history, &newest] (size_t index) {
            const auto& frame = history.at (index);
            return frame.sampleRate > 0u
                ? (double) (newest.endpoint - frame.endpoint) / (double) frame.sampleRate
                : absolute_spectrum::historySeconds; },
        [&history] (size_t index, float from, float to) {
            // Every band whose centre lies in the column counts; a column narrower than one band
            // takes the band under its middle.
            const auto& bands = history.at (index).postDbfs;
            const auto count = (int) KIRIN_SPECTRUM_BAND_COUNT;
            auto first = (int) std::ceil (from * (float) count - 0.5f);
            auto last = (int) std::floor (to * (float) count - 0.5f);
            if (last < first)
                first = last = (int) std::lround (0.5f * (from + to) * (float) count - 0.5f);
            first = juce::jlimit (0, count - 1, first);
            last = juce::jlimit (0, count - 1, last);
            auto peak = bands[(size_t) first];
            for (int band = first + 1; band <= last; ++band)
                peak = std::max (peak, bands[(size_t) band]);
            return peak - levelFloorDbfs; } };
    paint (g, plot, source, levelScale (plot), COL_SPECTRUM_POST, BG.darker (0.48f),
           absolute_spectrum::historySeconds);
    return true;
}

void paintInstrumentNotes (juce::Graphics& g, juce::Rectangle<float> plot,
                           const KirinSpectrumView& view, bool delta,
                           presentation::Context context)
{
    if (plot.getWidth() < minimumPlotWidth || plot.getHeight() < 140.0f)
        return;
    // Corner marks frame the measuring field like the reticle of an instrument.
    constexpr float corner = 7.0f;
    const auto inner = plot.reduced (2.0f);
    juce::Path marks;
    for (const auto& [x, y, dx, dy] : { std::array<float, 4> { inner.getX(), inner.getY(), 1.0f, 1.0f },
                                        std::array<float, 4> { inner.getRight(), inner.getY(), -1.0f, 1.0f },
                                        std::array<float, 4> { inner.getX(), inner.getBottom(), 1.0f, -1.0f },
                                        std::array<float, 4> { inner.getRight(), inner.getBottom(), -1.0f, -1.0f } })
    {
        marks.startNewSubPath (x + dx * corner, y);
        marks.lineTo (x, y);
        marks.lineTo (x, y + dy * corner);
    }
    g.setColour (COL_OBSERVATORY_VALUE.withAlpha (0.42f));
    g.strokePath (marks, juce::PathStrokeType (0.9f));

    // What the plot draws and the exact analysis it shows, from the snapshot itself.
    g.setFont (monoFont (context, typography::TextRole::axis, typography::Composition::visualization));
    g.setColour (COL_TEXT_TERTIARY.withAlpha (0.85f));
    const auto notes = inner.reduced (10.0f, 6.0f).toNearestInt();
    g.drawText (delta ? hypha::delta() + "(f) = POST(f) - PRE(f)  dB" : juce::String ("L(f) = POST(f)"),
                notes, juce::Justification::topLeft, false);
    if (view.sample_rate > 0u && view.aperture_samples > 0u)
    {
        const auto apertureMs = 1'000.0 * (double) view.aperture_samples / (double) view.sample_rate;
        g.drawText ("APERTURE " + juce::String (apertureMs, 1) + " ms   FFT "
                        + juce::String ((int) view.fft_size) + "   "
                        + juce::String ((int) KIRIN_SPECTRUM_BAND_COUNT) + " BANDS   "
                        + juce::String (ui_contract::spectrumPresentationHz) + " Hz",
                    notes, juce::Justification::bottomLeft, false);
    }
}
}
