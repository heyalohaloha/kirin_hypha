#include "HyphaSpectrumTerrain.h"

#include "HyphaAbsoluteSpectrumHistory.h"
#include "HyphaMaterialCache.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace hypha::spectrum_terrain
{
namespace
{
constexpr int crossEvery = 6;         // a grid line along time every sixth column
constexpr float perspective = 2.4f;   // the oldest ridge is 1 / (1 + 2.4) of the front size
constexpr float curtainAlpha = 0.78f; // the slope below a ridge hides what lies behind it

float perspectiveScale (float depth) noexcept
{
    return 1.0f / (1.0f + perspective * depth);
}

struct Ridge
{
    bool valid = false;
    float depth = 0.0f;
    std::array<juce::Point<float>, columnCount> points {}; // raster pixels
};

// The landscape is rasterized here, front to back, into premultiplied pixels. `horizon` holds, per
// pixel column, the top of everything nearer that is already drawn; a farther ridge, its slope and
// its grid lines are drawn only above it (a floating horizon), so every pixel is written about once
// instead of being covered again by every nearer curtain. Spans blend with the ordinary over rule,
// and their partial first and last pixels carry their coverage, which antialiases them.
class Raster
{
public:
    explicit Raster (juce::Image& image)
        : data (image, juce::Image::BitmapData::readWrite),
          horizon ((size_t) data.width, (float) data.height)
    {}

    int width() const noexcept { return data.width; }
    float horizonAt (int x) const noexcept { return horizon[(size_t) x]; }
    void raiseHorizon (int x, float y) noexcept { horizon[(size_t) x] = std::min (horizon[(size_t) x], y); }

    void span (int x, float top, float bottom, juce::PixelARGB colour, float weight = 1.0f) noexcept
    {
        top = std::max (top, 0.0f);
        bottom = std::min (bottom, (float) data.height);
        if (x < 0 || x >= data.width || ! (bottom > top))
            return;
        for (int y = (int) top, last = (int) std::ceil (bottom) - 1; y <= last; ++y)
            put (x, y, colour, weight * (std::min (bottom, (float) y + 1.0f) - std::max (top, (float) y)));
    }

    void put (int x, int y, juce::PixelARGB colour, float coverage) noexcept
    {
        const auto extra = (juce::uint32) juce::jlimit (0, 256, (int) (coverage * 256.0f + 0.5f));
        if (extra > 0u)
            reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (x, y))->blend (colour, extra);
    }

    // A thin straight line, drawn only above the horizon.
    void segment (juce::Point<float> a, juce::Point<float> b, float thickness, juce::PixelARGB colour) noexcept
    {
        const auto steep = std::abs (b.y - a.y) >= std::abs (b.x - a.x);
        if (steep ? a.y > b.y : a.x > b.x)
            std::swap (a, b);
        const auto length = a.getDistanceFrom (b);
        const auto run = steep ? b.y - a.y : b.x - a.x;
        if (length < 0.01f || run <= 0.0f)
            return;
        const auto half = 0.5f * thickness * length / run; // across the run, per step along it
        const auto from = steep ? a.y : a.x;
        const auto to = steep ? b.y : b.x;
        for (int step = (int) std::floor (from); step < (int) std::ceil (to); ++step)
        {
            const auto lo = std::max ((float) step, from);
            const auto hi = std::min ((float) step + 1.0f, to);
            if (hi <= lo)
                continue;
            const auto t = (0.5f * (lo + hi) - from) / run;
            const auto centre = steep ? a.x + (b.x - a.x) * t : a.y + (b.y - a.y) * t;
            for (int across = (int) std::floor (centre - half); across <= (int) std::floor (centre + half); ++across)
            {
                const auto overlap = std::min (centre + half, (float) across + 1.0f)
                                   - std::max (centre - half, (float) across);
                const auto x = steep ? across : step;
                const auto y = steep ? step : across;
                if (x < 0 || x >= data.width || y < 0 || y >= data.height)
                    continue;
                // The part of this pixel that lies above everything nearer.
                const auto top = steep ? lo : (float) y;
                const auto bottom = steep ? hi : (float) y + 1.0f;
                const auto visible = std::min (bottom, horizon[(size_t) x]) - top;
                if (visible > 0.0f)
                    put (x, y, colour, overlap * (hi - lo) * visible / (bottom - top));
            }
        }
    }

private:
    juce::Image::BitmapData data;
    std::vector<float> horizon;
};

// Where a ridge line crosses one pixel column: its highest and lowest point (vertices inside the
// column included, so a one-column peak keeps its full height) and its length inside the column.
struct Column
{
    float top = 0.0f;
    float bottom = 0.0f;
    float length = 1.0f;
};

void trace (const std::array<juce::Point<float>, columnCount>& p, int first, int last,
            std::vector<Column>& columns) noexcept
{
    size_t vertex = 0u; // the last vertex at or left of the current column edge
    const auto heightAt = [&p, &vertex] (float x) {
        while (vertex + 2u < p.size() && p[vertex + 1u].x <= x)
            ++vertex;
        const auto& a = p[vertex];
        const auto& b = p[vertex + 1u];
        const auto t = b.x > a.x ? juce::jlimit (0.0f, 1.0f, (x - a.x) / (b.x - a.x)) : 0.0f;
        return a.y + (b.y - a.y) * t;
    };
    juce::Point<float> previous ((float) first, heightAt ((float) first));
    for (int x = first; x <= last; ++x)
    {
        auto& column = columns[(size_t) x];
        column = { previous.y, previous.y, 0.0f };
        const auto right = (float) x + 1.0f;
        for (auto inside = vertex + 1u; inside < p.size() && p[inside].x < right; ++inside)
            if (p[inside].x > previous.x)
            {
                column.top = std::min (column.top, p[inside].y);
                column.bottom = std::max (column.bottom, p[inside].y);
                column.length += previous.getDistanceFrom (p[inside]);
                previous = p[inside];
            }
        const juce::Point<float> edge (right, heightAt (right));
        column.top = std::min (column.top, edge.y);
        column.bottom = std::max (column.bottom, edge.y);
        column.length += previous.getDistanceFrom (edge);
        previous = edge;
    }
}

void draw (juce::Image& image, float dpi, juce::Rectangle<float> plot, const Source& source,
           const Scale& scale, juce::Colour ink, juce::Colour floor, double seconds)
{
    // Each ridge takes the measured frame nearest to its age. A ridge with no frame within half a
    // ridge spacing is left out, so a gap in the measurement stays empty instead of being bridged.
    const auto spacing = seconds / (double) (ridgeCount - 1);
    const auto toRaster = [&plot, dpi] (juce::Point<float> point) {
        return juce::Point<float> ((point.x - plot.getX()) * dpi, (point.y - plot.getY()) * dpi); };
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
            ridge.points[(size_t) column] = toRaster (project (plot, scale, ridge.depth, 0.5f * (from + to),
                                                               source.peakIn (frame, from, to)));
        }
    }

    Raster raster (image);
    const auto frontZeroY = (scale.frontZeroY - plot.getY()) * dpi;
    const auto curtainDepth = (scale.frontZeroY - project (plot, scale, 1.0f, 0.5f, 0.0f).y) * 0.14f * dpi;
    const auto curtainColour = floor.withAlpha (curtainAlpha).getPixelARGB();
    std::vector<Column> columns ((size_t) raster.width());
    for (int row = ridgeCount - 1; row >= 0; --row)
    {
        const auto& ridge = ridges[(size_t) row];
        if (! ridge.valid)
            continue;
        const auto light = 1.0f - 0.78f * ridge.depth; // older ridges are fainter
        const auto& p = ridge.points;
        const auto first = std::max (0, (int) std::ceil (p.front().x));
        const auto last = std::min (raster.width() - 1, (int) std::floor (p.back().x) - 1);
        if (last < first)
            continue;
        trace (p, first, last, columns);
        // The slope reaches only a little below the ridge's own floor, and the newest ridge keeps
        // none: the flat plot below stays as it is.
        if (ridge.depth > 0.03f)
        {
            const auto slopeBottom = std::min (frontZeroY, toRaster (project (plot, scale, ridge.depth, 0.5f, 0.0f)).y
                                                               + curtainDepth);
            for (int x = first; x <= last; ++x)
                raster.span (x, columns[(size_t) x].top, std::min (slopeBottom, raster.horizonAt (x)),
                             curtainColour);
        }
        // Grid lines along time join only neighbouring ridges; a missing ridge breaks them.
        if (row + 1 < ridgeCount && ridges[(size_t) row + 1u].valid)
        {
            const auto& nearer = ridges[(size_t) row + 1u];
            const auto crossColour = ink.withAlpha (0.20f * (1.0f - 0.78f * nearer.depth)).getPixelARGB();
            for (int column = crossEvery / 2; column < columnCount; column += crossEvery)
                raster.segment (p[(size_t) column], nearer.points[(size_t) column], 0.7f * dpi, crossColour);
        }
        const auto thickness = (0.6f + 0.5f * light) * dpi;
        const auto lineColour = ink.withAlpha (0.08f + 0.62f * light).getPixelARGB();
        for (int x = first; x <= last; ++x)
        {
            const auto& column = columns[(size_t) x];
            const auto rise = column.bottom - column.top;
            // A steep run fills its whole pixel column; the weight keeps the stroke's true area.
            const auto weight = std::min (1.0f, thickness * column.length / (rise + thickness));
            raster.span (x, column.top - 0.5f * thickness,
                         std::min (column.bottom + 0.5f * thickness, raster.horizonAt (x)), lineColour, weight);
            raster.raiseHorizon (x, column.top - 0.5f * thickness);
        }
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
    // One device-resolution raster, drawn once: the ridges never go through path filling.
    const auto reported = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto dpi = std::isfinite (reported) && reported > 0.0f ? std::min (reported, 4.0f) : 1.0f;
    const auto width = (int) std::ceil (plot.getWidth() * dpi);
    const auto height = (int) std::ceil (plot.getHeight() * dpi);
    auto raster = material_cache::scratchImage (width, height);
    if (! raster.isValid())
        return;
    draw (raster, dpi, plot, source, scale, ink, floor, seconds);
    const juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (plot.toNearestInt());
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (raster, { plot.getX(), plot.getY(), (float) width / dpi, (float) height / dpi });
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
