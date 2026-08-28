#include "SpectrumFocusTrailContractTest.h"

#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumFocusTrail.h"
#include "../src/HyphaSpectrumFocusTrailPainter.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaUiContract.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>

namespace hypha::tests
{
namespace
{
    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "Spectrum Focus Trail contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

   #define KIRIN_FOCUS_REQUIRE(expression) require ((expression), #expression, __LINE__)

    spectrum_focus::DeltaBins bins (float first, float last)
    {
        spectrum_focus::DeltaBins values {};
        for (size_t index = 0; index < values.size(); ++index)
        {
            const float position = static_cast<float> (index)
                                 / static_cast<float> (values.size() - 1u);
            values[index] = first + position * (last - first);
        }
        return values;
    }

    void verifyRate (uint32_t sampleRate)
    {
        spectrum_focus::FocusTrailHistory history;
        const int64_t cadence = sampleRate / ui_contract::spectrumPresentationHz;
        const auto values = bins (-18.0f, 18.0f);
        for (size_t index = 1u; index <= spectrum_focus::focusTrailCapacity; ++index)
        {
            KIRIN_FOCUS_REQUIRE (history.append (
                static_cast<int64_t> (index) * cadence, sampleRate, values)
                == spectrum_focus::AppendResult::appended);
        }
        KIRIN_FOCUS_REQUIRE (history.size() == spectrum_focus::focusTrailCapacity);
        KIRIN_FOCUS_REQUIRE (history.currentSampleRate() == sampleRate);
        KIRIN_FOCUS_REQUIRE (history.endpointAt (history.size() - 1u)
                             == static_cast<int64_t> (
                                 spectrum_focus::focusTrailCapacity) * cadence);
        const double expectedAge = static_cast<double> (
            static_cast<int64_t> (spectrum_focus::focusTrailCapacity - 1u) * cadence)
            / sampleRate;
        KIRIN_FOCUS_REQUIRE (std::abs (history.ageSecondsAt (0u) - expectedAge) < 1.0e-9);
    }

    int differentPixels (const juce::Image& left,
                         const juce::Image& right,
                         juce::Rectangle<int> area)
    {
        KIRIN_FOCUS_REQUIRE (left.getBounds() == right.getBounds());
        int count = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (left.getPixelAt (x, y).getARGB()
                    != right.getPixelAt (x, y).getARGB())
                    ++count;
        return count;
    }

    juce::Image paintImage (SpectrumComponent& component)
    {
        juce::Image image (
            juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        image.clear (image.getBounds(), BG);
        juce::Graphics graphics (image);
        component.paintEntireComponent (graphics, true);
        return image;
    }

    double meanPaintMs (SpectrumComponent& component)
    {
        constexpr int iterations = 200;
        juce::Image image (
            juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        const double started = juce::Time::getMillisecondCounterHiRes();
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            image.clear (image.getBounds(), BG);
            juce::Graphics graphics (image);
            component.paintEntireComponent (graphics, true);
        }
        return (juce::Time::getMillisecondCounterHiRes() - started) / iterations;
    }

    double meanTrailPaintMs (const spectrum_focus::FocusTrailHistory& history,
                             juce::Rectangle<float> bounds,
                             float scale)
    {
        constexpr int iterations = 500;
        juce::Image image (juce::Image::ARGB,
                           juce::jmax (1, juce::roundToInt (bounds.getRight())),
                           juce::jmax (1, juce::roundToInt (bounds.getBottom())), true);
        const double started = juce::Time::getMillisecondCounterHiRes();
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            image.clear (image.getBounds(), BG);
            juce::Graphics graphics (image);
            spectrum_focus_painter::paint (
                graphics, bounds, scale, history, 0.70f, scale <= 1.1f);
        }
        return (juce::Time::getMillisecondCounterHiRes() - started) / iterations;
    }

    void writeImage (const juce::Image& image, const char* environmentVariable)
    {
        const auto path = juce::SystemStats::getEnvironmentVariable (
            environmentVariable, {});
        if (path.isEmpty())
            return;
        auto output = juce::File (path).createOutputStream();
        KIRIN_FOCUS_REQUIRE (output != nullptr);
        KIRIN_FOCUS_REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *output));
    }

    void verifyRenderingAtSize (const KirinSpectrumView& source,
                                const ui_contract::SpectrumSizePreset& preset,
                                const char* environmentVariable,
                                double totalBudgetMs)
    {
        SpectrumComponent component;
        const auto componentBounds = ui_contract::spectrumPlotBounds (
            preset.width, preset.height);
        component.setSize (componentBounds.width, componentBounds.height);
        const int64_t cadence = static_cast<int64_t> (
            source.sample_rate / ui_contract::spectrumPresentationHz);
        spectrum_focus::FocusTrailHistory directHistory;
        for (size_t frame = 0u; frame < spectrum_focus::focusTrailCapacity; ++frame)
        {
            KirinSpectrumView animated = source;
            animated.presentation_end_samples = source.presentation_end_samples
                                                + static_cast<int64_t> (frame) * cadence;
            for (size_t band = 0u; band < KIRIN_SPECTRUM_BAND_COUNT; ++band)
            {
                const float position = static_cast<float> (band)
                                     / static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT - 1u);
                const float focusedRegion = std::exp (
                    -std::pow ((position - 0.70f) / 0.16f, 2.0f));
                const float movement = 2.8f * focusedRegion * std::sin (
                    static_cast<float> (frame) * 0.17f
                    + static_cast<float> (band) * 0.012f);
                animated.display_db[band] = source.display_db[band] + movement;
                animated.post_dbfs[band] = animated.pre_dbfs[band]
                                           + animated.display_db[band];
            }
            component.setSnapshot (animated);
            spectrum_focus::DeltaBins directDelta {};
            std::copy (std::begin (animated.display_db), std::end (animated.display_db),
                       directDelta.begin());
            KIRIN_FOCUS_REQUIRE (directHistory.append (
                animated.presentation_end_samples, animated.sample_rate, directDelta)
                == spectrum_focus::AppendResult::appended);
        }
        KIRIN_FOCUS_REQUIRE (
            component.focusTrailSizeForTest() == spectrum_focus::focusTrailCapacity);

        const double unlockedPaintMs = meanPaintMs (component);
        const auto unlocked = paintImage (component);
        const auto bounds = component.getLocalBounds().toFloat();
        const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds);
        const float focusX = plot.getX() + 0.70f * plot.getWidth();
        const float focusY = plot.getCentreY();
        const auto eventTime = juce::Time::getCurrentTime();
        const juce::MouseEvent focusEvent (
            juce::Desktop::getInstance().getMainMouseSource(),
            { focusX, focusY }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &component, &component, eventTime,
            { focusX, focusY }, eventTime, 0, false);
        component.mouseMove (focusEvent);
        component.mouseDown (focusEvent);
        component.mouseExit (focusEvent);
        component.presentationTick();
        KIRIN_FOCUS_REQUIRE (component.hasFocusLock());

        const double focusedPaintMs = meanPaintMs (component);
        const auto focused = paintImage (component);
        const auto trailBounds = spectrum_geometry::focusTrailBoundsFor (bounds).toNearestInt();
        const double trailOnlyPaintMs = meanTrailPaintMs (
            directHistory, trailBounds.toFloat(),
            spectrum_geometry::visualScaleFor (bounds));
        std::cout << "Focus Trail paint " << preset.buttonText << ": "
                  << unlockedPaintMs << " -> " << focusedPaintMs
                  << " ms/frame, trail-only=" << trailOnlyPaintMs << " ms\n";
        KIRIN_FOCUS_REQUIRE (differentPixels (unlocked, focused, trailBounds) > 30);
        KIRIN_FOCUS_REQUIRE (focusedPaintMs < totalBudgetMs);
        KIRIN_FOCUS_REQUIRE (trailOnlyPaintMs < 0.5);
        writeImage (focused, environmentVariable);
    }
}

void verifySpectrumFocusTrailContract()
{
    using spectrum_focus::AppendResult;
    using spectrum_focus::FocusTrailHistory;

    KIRIN_FOCUS_REQUIRE (spectrum_focus::focusTrailCapacity == 180u);
    KIRIN_FOCUS_REQUIRE (sizeof (FocusTrailHistory) < 256u * 1024u);
    verifyRate (44'100u);
    verifyRate (48'000u);
    verifyRate (96'000u);

    FocusTrailHistory history;
    const auto rising = bins (-12.0f, 12.0f);
    KIRIN_FOCUS_REQUIRE (history.append (-1'600, 48'000u, rising)
                         == AppendResult::appended);
    KIRIN_FOCUS_REQUIRE (history.append (0, 48'000u, rising)
                         == AppendResult::appended);
    KIRIN_FOCUS_REQUIRE (history.append (0, 48'000u, bins (6.0f, -6.0f))
                         == AppendResult::duplicateIgnored);
    KIRIN_FOCUS_REQUIRE (history.size() == 2u);
    KIRIN_FOCUS_REQUIRE (std::abs (history.valueAt (1u, 0.0f) + 12.0f) < 0.001f);
    KIRIN_FOCUS_REQUIRE (std::abs (history.valueAt (1u, 1.0f) - 12.0f) < 0.001f);
    KIRIN_FOCUS_REQUIRE (std::abs (history.valueAt (1u, 0.5f)) < 0.1f);

    KIRIN_FOCUS_REQUIRE (history.append (3'200, 48'000u, rising)
                         == AppendResult::discontinuityReset);
    KIRIN_FOCUS_REQUIRE (history.size() == 1u && history.endpointAt (0u) == 3'200);
    KIRIN_FOCUS_REQUIRE (history.append (1'600, 48'000u, rising)
                         == AppendResult::discontinuityReset);
    KIRIN_FOCUS_REQUIRE (history.size() == 1u && history.endpointAt (0u) == 1'600);
    KIRIN_FOCUS_REQUIRE (history.append (2'940, 44'100u, rising)
                         == AppendResult::discontinuityReset);
    KIRIN_FOCUS_REQUIRE (history.currentSampleRate() == 44'100u);

    auto invalid = rising;
    invalid[7] = std::numeric_limits<float>::quiet_NaN();
    KIRIN_FOCUS_REQUIRE (history.append (4'410, 44'100u, invalid)
                         == AppendResult::rejected);
    KIRIN_FOCUS_REQUIRE (history.size() == 1u && history.endpointAt (0u) == 2'940);

    history.clear();
    const int64_t cadence = 1'600;
    for (size_t index = 1u; index <= spectrum_focus::focusTrailCapacity + 1u; ++index)
        KIRIN_FOCUS_REQUIRE (history.append (
            static_cast<int64_t> (index) * cadence, 48'000u, rising)
            == AppendResult::appended);
    KIRIN_FOCUS_REQUIRE (history.size() == spectrum_focus::focusTrailCapacity);
    KIRIN_FOCUS_REQUIRE (history.endpointAt (0u) == 2 * cadence);
    KIRIN_FOCUS_REQUIRE (history.endpointAt (history.size() - 1u)
                         == static_cast<int64_t> (
                             spectrum_focus::focusTrailCapacity + 1u) * cadence);
}

void verifySpectrumFocusTrailRendering (const KirinSpectrumView& snapshot)
{
    verifyRenderingAtSize (snapshot, ui_contract::spectrumSizePresets[0],
                           "KIRIN_UI_FOCUS_TRAIL_OUTPUT", 4.0);
    verifyRenderingAtSize (snapshot, ui_contract::spectrumSizePresets[1],
                           "KIRIN_UI_FOCUS_TRAIL_OUTPUT_MEDIUM", 6.0);
    verifyRenderingAtSize (snapshot, ui_contract::spectrumSizePresets[2],
                           "KIRIN_UI_FOCUS_TRAIL_OUTPUT_LARGE", 8.0);
}
}
