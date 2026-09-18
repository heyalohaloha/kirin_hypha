#include "AbsoluteSpectrumContractTest.h"

#include "../src/HyphaAbsoluteSpectrumHistory.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSpectrumPainter.h"
#include "../src/HyphaSpectrumUiContract.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#include <algorithm>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Absolute Spectrum contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_ABSOLUTE_SPECTRUM_REQUIRE(expression) \
    require ((expression), #expression, __LINE__)

KirinSpectrumView postFrame (int64_t endpoint, float magnitude)
{
    KirinSpectrumView frame {};
    frame.status = KIRIN_SPECTRUM_NO_PAIR;
    frame.has_data = 0;
    frame.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    frame.channels = 2;
    frame.sample_rate = 48'000;
    frame.min_hz = 10.0f;
    frame.max_hz = 22'000.0f;
    frame.presentation_end_samples = endpoint;
    frame.aperture_samples = 4'096;
    frame.fft_size = 8'192;
    frame.approximate_below_hz = 35.15625f;
    frame.post_has_data = 1;
    for (size_t index = 0u; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
        frame.post_dbfs[index] = magnitude - 8.0f
            * std::sin ((float) index * 0.03125f);
    return frame;
}

// One quiet frame with a single loud band, so the band's position and brightness are checkable.
KirinSpectrumView markedFrame (int64_t endpoint, size_t loudBand, float loudDbfs)
{
    KirinSpectrumView frame {};
    frame.status = KIRIN_SPECTRUM_NO_PAIR;
    frame.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    frame.channels = 2;
    frame.sample_rate = 48'000;
    frame.min_hz = 10.0f;
    frame.max_hz = 22'000.0f;
    frame.presentation_end_samples = endpoint;
    frame.aperture_samples = 4'096;
    frame.fft_size = 8'192;
    frame.approximate_below_hz = 35.15625f;
    frame.post_has_data = 1;
    for (auto& value : frame.post_dbfs)
        value = -90.0f;
    frame.post_dbfs[loudBand] = loudDbfs;
    return frame;
}

int fieldInkAt (const juce::Image& image, int x, int y)
{
    return image.getPixelAt (x, y).getAlpha();
}

// Rows of the rendered plot that carry field ink, ignoring the strip at the floor where the live
// curve is drawn.
std::vector<bool> fieldRowsWithInk (const juce::Image& image, juce::Rectangle<int> plot)
{
    std::vector<bool> inked;
    for (int y = plot.getY(); y < plot.getBottom() - 12; ++y)
    {
        bool any = false;
        for (int x = plot.getX(); x < plot.getRight() && ! any; ++x)
            any = fieldInkAt (image, x, y) > 0;
        inked.push_back (any);
    }
    return inked;
}

juce::Image renderField (const absolute_spectrum::History& history, int width, int height)
{
    spectrum_painter::SpectrumBins post {};
    spectrum_painter::SpectrumBins hold {};
    post.fill (-96.0f);
    hold.fill (-96.0f);
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    const auto plot = spectrum_geometry::dataPlotBoundsFor (image.getBounds().toFloat(), false);
    spectrum_painter::paintAbsolute (
        graphics, plot, spectrum_geometry::visualScaleFor (image.getBounds().toFloat()),
        post, hold, history, presentation::forEditor (width, height));
    return image;
}

// A host publishes on its own buffer boundaries, so the observations do not arrive one per row of
// the field. The field must still be continuous: the only empty rows allowed are the ones a real
// measurement gap creates.
void verifyFieldIsContinuousAtAnyHostCadence()
{
    constexpr int width = 600;
    constexpr int height = 400;

    for (const auto spacingSamples : { 1'600, 1'536, 2'048, 2'560, 3'200 })
    {
        absolute_spectrum::History history;
        for (int step = 1; step <= 400; ++step)
            KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.append (
                markedFrame ((int64_t) step * spacingSamples, 100u, -20.0f)));

        const auto image = renderField (history, width, height);
        const auto plot = spectrum_geometry::dataPlotBoundsFor (
            image.getBounds().toFloat(), false).toNearestInt();
        // Rows above the oldest observation are legitimately empty: a host that publishes faster
        // than 30 Hz fills the 180-frame ring in less than six seconds, and the field must not
        // invent the time it has no observations for. Everything from there down is continuous.
        const auto cadencePath = juce::SystemStats::getEnvironmentVariable (
            "KIRIN_HYPHA_FIELD_CADENCE_TEST_PNG", {});
        if (cadencePath.isNotEmpty() && spacingSamples == 2'048)
        {
            auto output = juce::File (cadencePath).createOutputStream();
            KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (output != nullptr);
            KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
                juce::PNGImageFormat().writeImageToStream (image, *output));
        }

        const auto inked = fieldRowsWithInk (image, plot);
        const auto firstInked = std::find (inked.begin(), inked.end(), true);
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (firstInked != inked.end());
        int longestEmptyRun = 0;
        int run = 0;
        for (auto row = firstInked; row != inked.end(); ++row)
        {
            run = *row ? 0 : run + 1;
            longestEmptyRun = std::max (longestEmptyRun, run);
        }
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (longestEmptyRun <= 2);
    }
}

// A real break in the measurement is not filled in. Half a second of missing observations has to
// stay visible as a band of empty rows.
void verifyFieldKeepsARealGapEmpty()
{
    constexpr int width = 600;
    constexpr int height = 400;
    constexpr int64_t spacing = 1'600;

    absolute_spectrum::History history;
    for (int step = 1; step <= 400; ++step)
    {
        // 0.5 s .. 1.0 s before the newest observation is missing.
        const auto ageSeconds = (double) (400 - step) * (double) spacing / 48'000.0;
        if (ageSeconds >= 0.5 && ageSeconds <= 1.0)
            continue;
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.append (
            markedFrame ((int64_t) step * spacing, 100u, -20.0f)));
    }

    const auto image = renderField (history, width, height);
    const auto plot = spectrum_geometry::dataPlotBoundsFor (
        image.getBounds().toFloat(), false).toNearestInt();
    const auto inked = fieldRowsWithInk (image, plot);
    int longestEmptyRun = 0;
    int run = 0;
    for (const auto row : inked)
    {
        run = row ? 0 : run + 1;
        longestEmptyRun = std::max (longestEmptyRun, run);
    }
    // 0.5 s of six seconds across the plot height, less the rounding at both ends.
    const int expected = (int) (0.5 / absolute_spectrum::historySeconds
                                * (double) (plot.getHeight() - 12));
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (longestEmptyRun >= expected - 4);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (longestEmptyRun <= expected + 4);
}

// The six-second field carries time on the vertical axis: the newest observation at the bottom,
// the oldest at the top. A sweeping loud band therefore has to draw one diagonal streak, and the
// streak's resolution is what tells the user whether a change was sudden or gradual.
void verifySixSecondFieldReadsAsTime()
{
    constexpr int width = 600;
    constexpr int height = 400;
    constexpr size_t frames = absolute_spectrum::historyCapacity;
    constexpr size_t oldestBand = 40u;
    constexpr size_t newestBand = 200u;

    absolute_spectrum::History history;
    for (size_t index = 0u; index < frames; ++index)
    {
        // 30 Hz observations, one per row of the field.
        const auto endpoint = (int64_t) ((index + 1u) * 1'600u);
        const auto band = oldestBand
            + (newestBand - oldestBand) * index / (frames - 1u);
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
            history.append (markedFrame (endpoint, band, -6.0f)));
    }
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.size() == frames);

    spectrum_painter::SpectrumBins post {};
    spectrum_painter::SpectrumBins hold {};
    post.fill (-96.0f);
    hold.fill (-96.0f);

    juce::Image image (juce::Image::ARGB, width, height, true);
    {
        juce::Graphics graphics (image);
        const auto plot = spectrum_geometry::dataPlotBoundsFor (
            image.getBounds().toFloat(), false);
        spectrum_painter::paintAbsolute (
            graphics, plot, spectrum_geometry::visualScaleFor (image.getBounds().toFloat()),
            post, hold, history, presentation::forEditor (width, height));

        const auto pixels = plot.toNearestInt();
        const auto columnFor = [&plot] (size_t band) {
            return juce::roundToInt (juce::jmap (
                spectrum_geometry::bandCentreNormalisedX (band),
                plot.getX(), plot.getRight()));
        };

        // 1. Age maps to height. Frame i is (179 - i) / 30 seconds old, so its loud band has to
        //    land that fraction of the way up from the bottom. Scanning stops short of the live
        //    curve's own row at the plot floor.
        const auto brightestRowIn = [&] (int column, int stopBefore) {
            int bestRow = -1;
            int bestInk = 0;
            for (int y = pixels.getY(); y < stopBefore; ++y)
                if (const auto ink = fieldInkAt (image, column, y); ink > bestInk)
                {
                    bestInk = ink;
                    bestRow = y;
                }
            return std::pair<int, int> { bestRow, bestInk };
        };
        const int scanStop = pixels.getBottom() - 12;
        const float tolerance = 0.03f * (float) pixels.getHeight();
        for (const auto frameIndex : { (size_t) 29u, (size_t) 89u, (size_t) 149u })
        {
            const auto band = oldestBand
                + (newestBand - oldestBand) * frameIndex / (frames - 1u);
            const double ageSeconds = (double) (frames - 1u - frameIndex) / 30.0;
            const float expected = (float) pixels.getBottom()
                - (float) (ageSeconds / absolute_spectrum::historySeconds)
                    * (float) pixels.getHeight();
            const auto found = brightestRowIn (columnFor (band), scanStop);
            KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (found.second > 0);
            KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
                std::abs ((float) found.first - expected) <= tolerance);
        }

        // 2. Time resolution. The field carries the 30 Hz observations, not a 40-row reduction of
        //    them, so a six-second sweep has to leave far more than 40 distinct rows of ink.
        int inkedRows = 0;
        for (int y = pixels.getY(); y < pixels.getBottom(); ++y)
        {
            bool inked = false;
            for (int x = pixels.getX(); x < pixels.getRight() && ! inked; ++x)
                inked = fieldInkAt (image, x, y) > 0;
            inkedRows += inked ? 1 : 0;
        }
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (inkedRows > 100);
    }

    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_SIX_SECOND_FIELD_TEST_PNG", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (output != nullptr);
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (image, *output));
    }

    // 3. Level separation. A loud band and a quiet one must not arrive at the same density, which
    //    is what made the field read as one flat haze.
    juce::Image loudImage (juce::Image::ARGB, width, height, true);
    juce::Image quietImage (juce::Image::ARGB, width, height, true);
    const auto inkForSingle = [&] (juce::Image& target, float dbfs) {
        // The marked observation is followed by two seconds of quiet ones, so its row sits well
        // clear of the live curve at the plot floor.
        absolute_spectrum::History single;
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (single.append (markedFrame (1'600, 100u, dbfs)));
        for (int later = 1; later <= 60; ++later)
            KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
                single.append (markedFrame ((int64_t) (later + 1) * 1'600, 250u, -90.0f)));
        juce::Graphics graphics (target);
        const auto plot = spectrum_geometry::dataPlotBoundsFor (
            target.getBounds().toFloat(), false);
        spectrum_painter::paintAbsolute (
            graphics, plot, spectrum_geometry::visualScaleFor (target.getBounds().toFloat()),
            post, hold, single, presentation::forEditor (width, height));
        const auto column = juce::roundToInt (juce::jmap (
            spectrum_geometry::bandCentreNormalisedX (100u), plot.getX(), plot.getRight()));
        int best = 0;
        for (int y = plot.toNearestInt().getY(); y < plot.toNearestInt().getBottom() - 12; ++y)
            best = std::max (best, fieldInkAt (target, column, y));
        return best;
    };
    const auto loudInk = inkForSingle (loudImage, -6.0f);
    const auto quietInk = inkForSingle (quietImage, -60.0f);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (loudInk > 0 && quietInk > 0);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (loudInk >= quietInk * 3);

}
}

void verifyAbsoluteSpectrumContract()
{
    verifySixSecondFieldReadsAsTime();
    verifyFieldIsContinuousAtAnyHostCadence();
    verifyFieldKeepsARealGapEmpty();

    absolute_spectrum::History history;
    const auto first = postFrame (4'800, -32.0f);
    const auto second = postFrame (6'400, -20.0f);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.append (first));
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.append (second));
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (! history.append (second));
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.size() == 2u);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
        std::abs (history.peakHold()[0] - second.post_dbfs[0]) < 0.0001f);

    auto invalid = postFrame (8'000, -12.0f);
    invalid.post_dbfs[17] = std::numeric_limits<float>::quiet_NaN();
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (! history.append (invalid));
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.size() == 2u);

    const auto backwards = postFrame (3'200, -26.0f);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.append (backwards));
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (history.size() == 1u);

    SpectrumComponent component;
    component.setPresentationContext (presentation::forEditor (600, 400));
    component.setSignalActive (true);
    component.setSize (600, 318);
    component.setAbsoluteObservation (true);
    KirinSpectrumBatch batch {};
    batch.count = 2;
    batch.frames[0] = first;
    batch.frames[1] = second;
    batch.latest = second;
    component.setBatch (batch);
    for (size_t index = 2u; index < absolute_spectrum::historyCapacity; ++index)
    {
        const float magnitude = -38.0f + 17.0f
            * std::sin ((float) index * 0.093f);
        component.queueSnapshot (postFrame (
            4'800 + static_cast<int64_t> (index) * 1'600, magnitude));
    }
    component.presentationTickAt (1'000.0);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (component.isAbsoluteObservationForTest());
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
        component.absoluteHistorySizeForTest() == absolute_spectrum::historyCapacity);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
        std::abs (component.absolutePeakHoldForTest (0) - second.post_dbfs[0]) < 0.0001f);

    SpectrumComponent deltaComponent;
    deltaComponent.setPresentationContext (presentation::forEditor (600, 400));
    deltaComponent.setSignalActive (true);
    deltaComponent.setBatch (batch);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
        deltaComponent.presentedEndpointForTest() == second.presentation_end_samples);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
        deltaComponent.absoluteHistorySizeForTest() == 0u);

    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    constexpr int paintIterations = 40;
    const double startedMs = juce::Time::getMillisecondCounterHiRes();
    for (int iteration = 0; iteration < paintIterations; ++iteration)
    {
        image.clear (image.getBounds(), juce::Colours::transparentBlack);
        juce::Graphics graphics (image);
        component.paintEntireComponent (graphics, true);
    }
    const double paintMs = (juce::Time::getMillisecondCounterHiRes() - startedMs)
                         / paintIterations;
    std::cout << "Absolute Spectrum 200% paint: " << paintMs << " ms/frame\n";
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (paintMs < 12.0);
    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_ABSOLUTE_SPECTRUM_TEST_PNG", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (output != nullptr);
        KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (image, *output));
    }
    int visiblePixels = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            visiblePixels += image.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (visiblePixels > 2'000);

    component.setAbsoluteObservation (false);
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (! component.isAbsoluteObservationForTest());
    KIRIN_ABSOLUTE_SPECTRUM_REQUIRE (
        component.absoluteHistorySizeForTest() == absolute_spectrum::historyCapacity);
}
}
