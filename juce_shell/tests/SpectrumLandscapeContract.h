#pragma once

#include "../src/HyphaAbsoluteSpectrumHistory.h"
#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSpectrumTerrain.h"
#include "../src/HyphaTheme.h"
#include "../src/HyphaUiContract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

// FREQ landscape contract. The landscape is a picture of measured history: each ridge is a quarter
// second on the running clock and holds the highest level of its frames, so a hit's top stands
// where its quarter's age, its frequency and its level put it however briefly it lasted; a ridge
// moves back with time rather than being redrawn in place; older ridges read farther away; and
// nothing may appear where no measurement exists: a quarter without frames stays empty, and two
// lone observations do not grow a range between them. It is painted over the opaque page colour,
// so ink is brightness above that colour; the curtains that hide farther ridges only darken.
namespace hypha::tests
{
namespace landscape_contract
{
inline void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "FREQ landscape contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_LANDSCAPE_REQUIRE(expression) \
    hypha::tests::landscape_contract::require ((expression), #expression, __LINE__)

constexpr int64_t frameSamples = 1'600; // 30 Hz at 48 kHz
constexpr size_t frames = absolute_spectrum::historyCapacity;
constexpr float quietDbfs = -90.0f;
constexpr int inkThreshold = 24;        // of 487 for the full ink colour over the page

inline juce::Rectangle<float> componentBounds (int editorWidth, int editorHeight)
{
    const auto bounds = ui_contract::spectrumPlotBounds (editorWidth, editorHeight);
    return { 0.0f, 0.0f, (float) bounds.width, (float) bounds.height };
}

// The 200% editor, the narrowest one that draws the landscape.
inline juce::Rectangle<float> plotBounds()
{
    return spectrum_geometry::dataPlotBoundsFor (componentBounds (600, 400), false);
}

inline KirinSpectrumView frameWith (int64_t endpoint, size_t loudBand, float loudDbfs)
{
    KirinSpectrumView view {};
    view.status = KIRIN_SPECTRUM_NO_PAIR;
    view.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    view.channels = 2;
    view.sample_rate = 48'000;
    view.min_hz = 10.0f;
    view.max_hz = 22'000.0f;
    view.presentation_end_samples = endpoint;
    view.aperture_samples = 4'096;
    view.fft_size = 8'192;
    view.post_has_data = 1;
    for (auto& value : view.post_dbfs)
        value = quietDbfs;
    view.post_dbfs[loudBand] = loudDbfs;
    return view;
}

struct Render
{
    juce::Image image;
    bool drawn = false;
};

inline Render render (const absolute_spectrum::History& history,
                      juce::Rectangle<float> bounds = componentBounds (600, 400))
{
    const auto size = bounds.toNearestInt();
    Render result { juce::Image (juce::Image::ARGB, size.getWidth(), size.getHeight(), true) };
    juce::Graphics g (result.image);
    g.fillAll (BG);
    result.drawn = spectrum_terrain::paintLevelLandscape (
        g, spectrum_geometry::dataPlotBoundsFor (bounds, false), history);
    return result;
}

inline int inkAt (const juce::Image& image, int x, int y)
{
    if (! image.getBounds().contains (x, y))
        return 0;
    const auto pixel = image.getPixelAt (x, y);
    const auto sum = [] (juce::Colour colour) {
        return (int) colour.getRed() + (int) colour.getGreen() + (int) colour.getBlue(); };
    return std::max (0, sum (pixel) - sum (BG));
}

inline int inkIn (const juce::Image& image, juce::Rectangle<float> area)
{
    int best = 0;
    for (int y = (int) std::floor (area.getY()); y <= (int) std::ceil (area.getBottom()); ++y)
        for (int x = (int) std::floor (area.getX()); x <= (int) std::ceil (area.getRight()); ++x)
            best = std::max (best, inkAt (image, x, y));
    return best;
}

inline int inkNear (const juce::Image& image, juce::Point<float> centre, float halfWidth,
                    float halfHeight)
{
    return inkIn (image, { centre.x - halfWidth, centre.y - halfHeight,
                           2.0f * halfWidth, 2.0f * halfHeight });
}

inline float ridgeWidth (juce::Rectangle<float> plot, float depth) noexcept
{
    const auto scale = spectrum_terrain::levelScale (plot);
    return spectrum_terrain::project (plot, scale, depth, 1.0f, 0.0f).x
         - spectrum_terrain::project (plot, scale, depth, 0.0f, 0.0f).x;
}

inline float quietLineY (juce::Rectangle<float> plot, float depth) noexcept
{
    return spectrum_terrain::project (plot, spectrum_terrain::levelScale (plot), depth, 0.5f,
                                      quietDbfs - spectrum_terrain::levelFloorDbfs).y;
}

// The newest ridge is the flat reading plot itself, and every older one lies higher, narrower
// and closer to the centre: the perspective that makes the stack read as depth.
inline void verifyPerspective()
{
    const auto plot = plotBounds();
    const auto scale = spectrum_terrain::levelScale (plot);
    for (const auto x : { 0.0f, 0.37f, 1.0f })
        for (const auto dbfs : { -96.0f, -48.0f, 0.0f })
        {
            const auto point = spectrum_terrain::project (
                plot, scale, 0.0f, x, dbfs - spectrum_terrain::levelFloorDbfs);
            KIRIN_LANDSCAPE_REQUIRE (std::abs (point.x - (plot.getX() + x * plot.getWidth())) < 0.01f);
            KIRIN_LANDSCAPE_REQUIRE (std::abs (point.y - juce::jmap (dbfs, 0.0f, -96.0f, plot.getY(),
                                                                     plot.getBottom())) < 0.01f);
        }
    for (int step = 1; step <= 10; ++step)
    {
        const auto depth = (float) step / 10.0f;
        const auto nearer = (float) (step - 1) / 10.0f;
        KIRIN_LANDSCAPE_REQUIRE (quietLineY (plot, depth) < quietLineY (plot, nearer));
        KIRIN_LANDSCAPE_REQUIRE (ridgeWidth (plot, depth) < ridgeWidth (plot, nearer));
        // The loudest level of the oldest ridge still fits inside the plot.
        KIRIN_LANDSCAPE_REQUIRE (spectrum_terrain::project (plot, scale, depth, 0.5f,
                                                            -spectrum_terrain::levelFloorDbfs).y
                                 >= plot.getY());
    }
}

inline double frameSeconds (size_t index) noexcept
{
    return (double) (index + 1u) * (double) frameSamples / 48'000.0;
}

inline float depthOfFrame (size_t index, size_t newest = frames - 1u) noexcept
{
    return (float) (spectrum_terrain::sliceAge (frameSeconds (index), frameSeconds (newest))
                    / absolute_spectrum::historySeconds);
}

// One frame in the middle of each checked quarter carries a loud band, each at its own frequency;
// every other frame is quiet. Each quarter's ridge holds that band's top where the quarter's age,
// the band and its level project, however briefly it lasted, and older tops are fainter.
inline void verifyEachQuarterKeepsItsTop()
{
    constexpr float loudDbfs = -6.0f;
    // Frames in the middle of quarters that end 5.0, 3.25, 1.75 and 0.75 s before the newest.
    constexpr std::array<size_t, 4> loudFrames { 25u, 78u, 123u, 153u };
    constexpr std::array<size_t, 4> bands { 60u, 110u, 160u, 210u };

    absolute_spectrum::History history;
    for (size_t index = 0u; index < frames; ++index)
    {
        auto view = frameWith ((int64_t) (index + 1u) * frameSamples, 20u, quietDbfs);
        for (size_t check = 0u; check < loudFrames.size(); ++check)
            if (index == loudFrames[check])
                view.post_dbfs[bands[check]] = loudDbfs;
        KIRIN_LANDSCAPE_REQUIRE (history.append (view));
    }
    const auto [image, drawn] = render (history);
    KIRIN_LANDSCAPE_REQUIRE (drawn);

    const auto plot = plotBounds();
    const auto scale = spectrum_terrain::levelScale (plot);
    std::array<int, 4> ink {};
    for (size_t check = 0u; check < loudFrames.size(); ++check)
    {
        const auto depth = depthOfFrame (loudFrames[check]);
        const auto peak = spectrum_terrain::project (
            plot, scale, depth, spectrum_geometry::bandCentreNormalisedX (bands[check]),
            loudDbfs - spectrum_terrain::levelFloorDbfs);
        // A ridge column is 1/96 of the ridge's width; the peak may sit anywhere in its column.
        const auto column = ridgeWidth (plot, depth) / (float) spectrum_terrain::columnCount;
        ink[check] = inkNear (image, peak, 0.5f * column + 1.5f, 2.5f);
        KIRIN_LANDSCAPE_REQUIRE (ink[check] >= inkThreshold);
    }
    KIRIN_LANDSCAPE_REQUIRE (ink.back() * 2 > ink.front() * 3);
}

// A ridge moves back with its quarter: a tenth of a second later the same top stands where the
// greater age puts it (closer to the centre and the horizon), not where it stood.
inline void verifyARidgeMovesBack()
{
    constexpr float topDbfs = -30.0f;
    constexpr size_t topFrame = 119u;
    constexpr size_t band = 40u;
    const auto plot = plotBounds();
    const auto scale = spectrum_terrain::levelScale (plot);
    const auto topAt = [&] (size_t newest) {
        absolute_spectrum::History history;
        for (size_t index = 0u; index <= newest; ++index)
        {
            auto view = frameWith ((int64_t) (index + 1u) * frameSamples, 20u, quietDbfs);
            if (index == topFrame)
                view.post_dbfs[band] = topDbfs;
            KIRIN_LANDSCAPE_REQUIRE (history.append (view));
        }
        const auto result = render (history);
        KIRIN_LANDSCAPE_REQUIRE (result.drawn);
        const auto depth = depthOfFrame (topFrame, newest);
        const auto point = spectrum_terrain::project (plot, scale, depth,
            spectrum_geometry::bandCentreNormalisedX (band), topDbfs - spectrum_terrain::levelFloorDbfs);
        const auto column = ridgeWidth (plot, depth) / (float) spectrum_terrain::columnCount;
        return std::pair { point, inkNear (result.image, point, 0.5f * column + 1.5f, 2.5f) };
    };
    const auto earlier = topAt (frames - 4u);
    const auto later = topAt (frames - 1u);
    KIRIN_LANDSCAPE_REQUIRE (earlier.second >= inkThreshold && later.second >= inkThreshold);
    KIRIN_LANDSCAPE_REQUIRE (earlier.first.getDistanceFrom (later.first) > 1.0f);
    KIRIN_LANDSCAPE_REQUIRE (later.first.x > earlier.first.x && later.first.y < earlier.first.y);
}

// A quarter with no frame has no ridge, and no grid line bridges the quarters on either side.
inline void verifyAGapStaysEmpty()
{
    // The quarter that ends 0.75 s before the newest frame, near the front where ridges stand apart.
    const auto newest = frameSeconds (frames - 1u);
    const auto missing = (std::int64_t) std::floor ((newest - 0.875) / spectrum_terrain::sliceSeconds);
    const auto inQuarter = [] (size_t index, std::int64_t quarter) {
        return (std::int64_t) std::floor (frameSeconds (index) / spectrum_terrain::sliceSeconds) == quarter; };
    const auto build = [&] (bool gap) {
        absolute_spectrum::History history;
        for (size_t index = 0u; index < frames; ++index)
            if (! gap || ! inQuarter (index, missing))
                KIRIN_LANDSCAPE_REQUIRE (history.append (
                    frameWith ((int64_t) (index + 1u) * frameSamples, 20u, quietDbfs)));
        return history;
    };
    const auto depthOfQuarter = [&] (std::int64_t quarter) {
        return (float) (spectrum_terrain::sliceAge ((double) quarter * spectrum_terrain::sliceSeconds, newest)
                        / absolute_spectrum::historySeconds); };

    // Between the ridges on either side of the missing one, clear of their own lines.
    const auto plot = plotBounds();
    const auto older = quietLineY (plot, depthOfQuarter (missing - 1));
    const auto newer = quietLineY (plot, depthOfQuarter (missing + 1));
    KIRIN_LANDSCAPE_REQUIRE (newer - older > 10.0f);
    const juce::Rectangle<float> between { plot.getX() + 1.0f, older + 2.5f,
                                           plot.getWidth() - 2.0f, newer - older - 5.0f };

    const auto complete = render (build (false));
    const auto gapped = render (build (true));
    KIRIN_LANDSCAPE_REQUIRE (complete.drawn && gapped.drawn);
    // The complete history draws the ridge there, so the check can see one.
    KIRIN_LANDSCAPE_REQUIRE (inkIn (complete.image, between) >= inkThreshold);
    KIRIN_LANDSCAPE_REQUIRE (inkIn (gapped.image, between) < inkThreshold);
    for (const auto line : { older, newer })
        KIRIN_LANDSCAPE_REQUIRE (inkNear (gapped.image, { plot.getCentreX(), line }, 2.0f, 2.0f)
                                 >= inkThreshold);
}

// Two observations five seconds apart are two ridges, not a range between them, and nothing is
// drawn in the sky above the oldest one.
inline void verifyTwoObservationsAreTwoRidges()
{
    absolute_spectrum::History history;
    KIRIN_LANDSCAPE_REQUIRE (history.append (frameWith (frameSamples, 20u, quietDbfs)));
    KIRIN_LANDSCAPE_REQUIRE (history.append (frameWith (frameSamples + 5 * 48'000, 20u, quietDbfs)));
    const auto [image, drawn] = render (history);
    KIRIN_LANDSCAPE_REQUIRE (drawn);

    const auto plot = plotBounds();
    const auto oldest = quietLineY (plot, (float) (spectrum_terrain::sliceAge (
        (double) frameSamples / 48'000.0, (double) (frameSamples + 5 * 48'000) / 48'000.0)
        / absolute_spectrum::historySeconds));
    const auto newest = quietLineY (plot, 0.0f);
    for (const auto line : { oldest, newest })
        KIRIN_LANDSCAPE_REQUIRE (inkNear (image, { plot.getCentreX(), line }, 2.0f, 2.0f)
                                 >= inkThreshold);
    KIRIN_LANDSCAPE_REQUIRE (inkIn (image, { plot.getX() + 1.0f, oldest + 2.5f,
                                             plot.getWidth() - 2.0f, newest - oldest - 5.0f })
                             < inkThreshold);
    KIRIN_LANDSCAPE_REQUIRE (inkIn (image, { plot.getX() + 1.0f, plot.getY() + 1.0f,
                                             plot.getWidth() - 2.0f, oldest - plot.getY() - 3.5f })
                             < inkThreshold);
}

// The landscape needs room. The 100%, 125% and 150% editors keep the flat six-second field, the
// 200% and 300% editors draw the landscape and its instrument notes, and a single observation
// is no landscape at all.
inline void verifyOnlyWideEditorsDrawTheLandscape()
{
    absolute_spectrum::History history;
    KIRIN_LANDSCAPE_REQUIRE (history.append (frameWith (frameSamples, 20u, -30.0f)));
    const auto single = render (history);
    KIRIN_LANDSCAPE_REQUIRE (! single.drawn);
    KIRIN_LANDSCAPE_REQUIRE (inkIn (single.image, plotBounds()) == 0);
    const auto view = frameWith (2 * frameSamples, 20u, -30.0f);
    KIRIN_LANDSCAPE_REQUIRE (history.append (view));

    for (const auto& preset : ui_contract::spectrumSizePresets)
    {
        const auto bounds = componentBounds (preset.width, preset.height);
        const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds, false);
        const bool wide = preset.width >= 600;
        KIRIN_LANDSCAPE_REQUIRE ((plot.getWidth() >= spectrum_terrain::minimumPlotWidth) == wide);
        KIRIN_LANDSCAPE_REQUIRE (render (history, bounds).drawn == wide);

        const auto size = bounds.toNearestInt();
        juce::Image notes (juce::Image::ARGB, size.getWidth(), size.getHeight(), true);
        {
            juce::Graphics g (notes);
            g.fillAll (BG);
            spectrum_terrain::paintInstrumentNotes (
                g, plot, view, false, presentation::forEditor (preset.width, preset.height));
        }
        // The corner mark at the upper left of the plot.
        const auto corner = inkNear (notes, plot.getTopLeft().translated (3.0f, 3.0f), 2.0f, 2.0f);
        KIRIN_LANDSCAPE_REQUIRE ((corner >= inkThreshold) == wide);
        if (! wide)
            KIRIN_LANDSCAPE_REQUIRE (inkIn (notes, notes.getBounds().toFloat()) == 0);
    }
}

// The 300% landscape at DPI 2 with a new frame every paint, as an open editor draws it. The ridges
// are rasterized once per paint without path filling, so they stay a small part of a frame.
inline void verifyLandscapeFrameBudget()
{
    material_cache::Lifetime editor;
    const auto bounds = componentBounds (900, 600);
    const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds, false);
    const auto busy = [] (int index) {
        auto view = frameWith ((int64_t) (index + 1) * frameSamples, 0u, quietDbfs);
        for (size_t band = 0u; band < KIRIN_SPECTRUM_BAND_COUNT; ++band)
            view.post_dbfs[band] = -42.0f + 18.0f * std::sin ((float) band * 0.07f + (float) index * 0.21f)
                                 + 9.0f * std::sin ((float) band * 0.31f - (float) index * 0.13f);
        return view;
    };
    absolute_spectrum::History history;
    for (int index = 0; index < (int) frames; ++index)
        KIRIN_LANDSCAPE_REQUIRE (history.append (busy (index)));
    juce::Image image (juce::Image::ARGB, (int) std::ceil (bounds.getWidth() * 2.0f),
                       (int) std::ceil (bounds.getHeight() * 2.0f), true);
    std::vector<double> samples;
    for (int index = 0; index < 33; ++index)
    {
        KIRIN_LANDSCAPE_REQUIRE (history.append (busy ((int) frames + index)));
        const auto start = juce::Time::getMillisecondCounterHiRes();
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            KIRIN_LANDSCAPE_REQUIRE (spectrum_terrain::paintLevelLandscape (g, plot, history));
        }
        if (index >= 3)
            samples.push_back (juce::Time::getMillisecondCounterHiRes() - start);
    }
    std::sort (samples.begin(), samples.end());
    const auto median = samples[samples.size() / 2];
    std::cout << "FREQ landscape 300% at DPI 2: " << median << " ms/frame\n";
    KIRIN_LANDSCAPE_REQUIRE (median < 12.0);
}
}

inline void verifyLevelLandscape()
{
    landscape_contract::verifyPerspective();
    landscape_contract::verifyEachQuarterKeepsItsTop();
    landscape_contract::verifyARidgeMovesBack();
    landscape_contract::verifyAGapStaysEmpty();
    landscape_contract::verifyTwoObservationsAreTwoRidges();
    landscape_contract::verifyOnlyWideEditorsDrawTheLandscape();
    landscape_contract::verifyLandscapeFrameBudget();
}
}
