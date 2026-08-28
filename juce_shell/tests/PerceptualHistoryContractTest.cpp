#include "PerceptualHistoryContractTest.h"

#include "../src/HyphaPerceptualComponent.h"
#include "../src/HyphaPerceptualHistory.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaUiContract.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace
{
    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "Perceptual history contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

#define KIRIN_PERCEPTUAL_REQUIRE(expression) require ((expression), #expression, __LINE__)

    KirinPerceptualView snapshot (int64_t endpoint, double delta)
    {
        KirinPerceptualView value {};
        value.status = KIRIN_SPECTRUM_ACTIVE;
        value.has_data = 1u;
        value.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
        value.channels = 2u;
        value.sample_rate = 48'000u;
        value.aperture_samples = 4'800u;
        value.pre_sharpness = 1.25;
        value.post_sharpness = value.pre_sharpness + delta;
        value.delta_sharpness = delta;
        value.presentation_end_samples = endpoint;
        value.state_epoch_samples = 0;
        return value;
    }

    KirinPerceptualBatch batch (int firstIndex, int lastIndex)
    {
        KirinPerceptualBatch value {};
        for (int index = firstIndex; index <= lastIndex; ++index)
            value.frames[value.count++] = snapshot (
                (int64_t) index * 4'800, 0.02 * (double) index);
        value.latest = value.frames[value.count - 1u];
        return value;
    }

    int visiblePixels (const juce::Image& image)
    {
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                count += image.getPixelAt (x, y).getAlpha() != 0 ? 1 : 0;
        return count;
    }

    int maximumAlphaAround (const juce::Image& image, juce::Point<int> centre)
    {
        int maximum = 0;
        for (int y = centre.y - 2; y <= centre.y + 2; ++y)
            for (int x = centre.x - 2; x <= centre.x + 2; ++x)
                if (image.getBounds().contains (x, y))
                    maximum = std::max (maximum, (int) image.getPixelAt (x, y).getAlpha());
        return maximum;
    }

    juce::Rectangle<float> perceptualPlotFor (juce::Rectangle<int> bounds)
    {
        const auto componentBounds = bounds.toFloat();
        const float scale = spectrum_geometry::visualScaleFor (componentBounds);
        auto plot = spectrum_geometry::plotBoundsFor (componentBounds);
        plot.removeFromTop ((scale > 1.1f ? 27.0f : 17.0f) * scale);
        plot.removeFromBottom (10.0f * scale);
        return plot;
    }

    float yForDelta (double delta, juce::Rectangle<float> plot)
    {
        return juce::jmap (static_cast<float> (delta), 2.0f, -2.0f,
                           plot.getY(), plot.getBottom());
    }

    juce::Point<int> pointAt (juce::Rectangle<float> plot,
                              double ageSeconds,
                              double delta)
    {
        const float x = plot.getRight()
                      - static_cast<float> (ageSeconds / perceptual_history::historySeconds)
                            * plot.getWidth();
        return { juce::roundToInt (x), juce::roundToInt (yForDelta (delta, plot)) };
    }

    void setSteadyHistory (PerceptualComponent& component,
                           double firstDelta,
                           double steadyDelta)
    {
        for (int index = 1; index <= 60; ++index)
        {
            const double delta = index == 1 ? firstDelta : steadyDelta;
            auto value = snapshot ((int64_t) index * 4'800, delta);
            value.pre_sharpness = 2.5;
            value.post_sharpness = value.pre_sharpness + delta;
            component.setSnapshot (value);
        }
    }

    juce::Image renderComponent (PerceptualComponent& component)
    {
        juce::Image image (juce::Image::ARGB,
                           component.getWidth(), component.getHeight(), true);
        juce::Graphics graphics (image);
        component.paintEntireComponent (graphics, true);
        return image;
    }

    juce::Image renderSteadyHistory (double firstDelta, double steadyDelta)
    {
        const auto& preset = ui_contract::spectrumSizePresets[2];
        const auto bounds = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        PerceptualComponent component;
        component.setSize (bounds.width, bounds.height);
        setSteadyHistory (component, firstDelta, steadyDelta);
        return renderComponent (component);
    }

    void verifyFillUsesZeroDistanceOnly()
    {
        const auto& preset = ui_contract::spectrumSizePresets[2];
        const auto bounds = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        const auto plot = perceptualPlotFor (
            juce::Rectangle<int> (0, 0, bounds.width, bounds.height));

        const auto positive = renderSteadyHistory (1.6, 1.6);
        const int positiveNear = positive.getPixelAt (
            pointAt (plot, 0.7, 0.25).x, pointAt (plot, 0.7, 0.25).y).getAlpha();
        const int positiveFar = positive.getPixelAt (
            pointAt (plot, 0.7, 1.25).x, pointAt (plot, 0.7, 1.25).y).getAlpha();
        std::cout << "Sharpness positive fill near/far alpha: "
                  << positiveNear << '/' << positiveFar << '\n';
        KIRIN_PERCEPTUAL_REQUIRE (positiveNear >= 32);
        KIRIN_PERCEPTUAL_REQUIRE (positiveFar >= positiveNear + 50);

        const auto negative = renderSteadyHistory (-1.6, -1.6);
        const int negativeNear = negative.getPixelAt (
            pointAt (plot, 0.7, -0.25).x, pointAt (plot, 0.7, -0.25).y).getAlpha();
        const int negativeFar = negative.getPixelAt (
            pointAt (plot, 0.7, -1.25).x, pointAt (plot, 0.7, -1.25).y).getAlpha();
        std::cout << "Sharpness negative fill near/far alpha: "
                  << negativeNear << '/' << negativeFar << '\n';
        KIRIN_PERCEPTUAL_REQUIRE (negativeNear >= 32);
        KIRIN_PERCEPTUAL_REQUIRE (negativeFar >= negativeNear + 50);

        const auto zeroHead = renderSteadyHistory (0.0, 1.6);
        const auto highHead = renderSteadyHistory (1.6, 1.6);
        const auto sample = pointAt (plot, 0.7, 0.8);
        const int zeroHeadAlpha = zeroHead.getPixelAt (sample.x, sample.y).getAlpha();
        const int highHeadAlpha = highHead.getPixelAt (sample.x, sample.y).getAlpha();
        std::cout << "Sharpness fill history-head independence alpha: "
                  << zeroHeadAlpha << '/' << highHeadAlpha << '\n';
        KIRIN_PERCEPTUAL_REQUIRE (zeroHeadAlpha > 0);
        KIRIN_PERCEPTUAL_REQUIRE (std::abs (zeroHeadAlpha - highHeadAlpha) <= 2);
    }

    void verifyHeldFillPersistsAfterPresentationStops()
    {
        const auto& preset = ui_contract::spectrumSizePresets[2];
        const auto bounds = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        const auto plot = perceptualPlotFor (
            juce::Rectangle<int> (0, 0, bounds.width, bounds.height));
        PerceptualComponent component;
        component.setSize (bounds.width, bounds.height);
        setSteadyHistory (component, 0.0, -1.6);

        const auto live = renderComponent (component);
        component.presentationTickAt (juce::Time::getMillisecondCounterHiRes() + 10'000.0);
        const auto held = renderComponent (component);
        const auto sample = pointAt (plot, 0.7, -0.8);
        const int liveAlpha = live.getPixelAt (sample.x, sample.y).getAlpha();
        const int heldAlpha = held.getPixelAt (sample.x, sample.y).getAlpha();
        std::cout << "Sharpness stopped fill persistence alpha: "
                  << liveAlpha << '/' << heldAlpha << '\n';
        KIRIN_PERCEPTUAL_REQUIRE (liveAlpha >= 50);
        KIRIN_PERCEPTUAL_REQUIRE (heldAlpha == liveAlpha);
    }

    void verifyUniformHistoryInk()
    {
        const auto& preset = ui_contract::spectrumSizePresets[2];
        const auto bounds = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        PerceptualComponent component;
        component.setSize (bounds.width, bounds.height);
        for (int index = 1; index <= 60; ++index)
            component.setSnapshot (snapshot ((int64_t) index * 4'800, 1.0));

        juce::Image image (juce::Image::ARGB, bounds.width, bounds.height, true);
        juce::Graphics graphics (image);
        component.paintEntireComponent (graphics, true);

        const auto plot = perceptualPlotFor (component.getLocalBounds());
        const int olderAlpha = maximumAlphaAround (image, pointAt (plot, 4.0, 1.0));
        const int newerAlpha = maximumAlphaAround (image, pointAt (plot, 0.5, 1.0));
        std::cout << "Sharpness uniform ink alpha: " << olderAlpha
                  << '/' << newerAlpha << '\n';
        KIRIN_PERCEPTUAL_REQUIRE (olderAlpha > 0);
        KIRIN_PERCEPTUAL_REQUIRE (newerAlpha > 0);
        KIRIN_PERCEPTUAL_REQUIRE (std::abs (olderAlpha - newerAlpha) <= 2);
    }

    double renderAt (const ui_contract::SpectrumSizePreset& preset,
                     const char* outputVariable)
    {
        PerceptualComponent component;
        const auto bounds = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        component.setSize (bounds.width, bounds.height);
        for (int index = 1; index <= 60; ++index)
        {
            const double delta = 0.72 * std::sin ((double) index * 0.21)
                               + 0.23 * std::sin ((double) index * 0.57);
            component.setSnapshot (snapshot ((int64_t) index * 4'800, delta));
        }
        KIRIN_PERCEPTUAL_REQUIRE (component.historySizeForTest() == 60u);
        juce::Image image (juce::Image::ARGB, bounds.width, bounds.height, true);
        // Font fallback and CoreGraphics caches are process-cold only for the first preset in
        // this console test. The shipped editor has already painted its meter page before SHARP
        // can be opened, so exclude that unrelated one-time initialization from the steady-frame
        // budget and measure every preset under the same warmed conditions.
        constexpr int warmupIterations = 8;
        for (int iteration = 0; iteration < warmupIterations; ++iteration)
        {
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            juce::Graphics graphics (image);
            component.paintEntireComponent (graphics, true);
        }
        constexpr int iterations = 200;
        const double started = juce::Time::getMillisecondCounterHiRes();
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            juce::Graphics graphics (image);
            component.paintEntireComponent (graphics, true);
        }
        const double paintMs = (juce::Time::getMillisecondCounterHiRes() - started) / iterations;
        KIRIN_PERCEPTUAL_REQUIRE (visiblePixels (image) > bounds.width * bounds.height / 10);

        const auto outputPath = juce::SystemStats::getEnvironmentVariable (outputVariable, {});
        if (outputPath.isNotEmpty())
        {
            auto output = juce::File (outputPath).createOutputStream();
            KIRIN_PERCEPTUAL_REQUIRE (output != nullptr);
            KIRIN_PERCEPTUAL_REQUIRE (
                juce::PNGImageFormat().writeImageToStream (image, *output));
        }
        return paintMs;
    }
}

void verifyPerceptualHistoryContract()
{
    using namespace perceptual_history;
    static_assert (historyCapacity == 60u);
    static_assert (historySeconds == 6.0);
    History history;
    KIRIN_PERCEPTUAL_REQUIRE (
        history.append (4'800, 48'000, 4'800, 1.0, 1.2, 0.2)
        == AppendResult::appended);
    KIRIN_PERCEPTUAL_REQUIRE (
        history.append (4'800, 48'000, 4'800, 1.0, 1.2, 0.2)
        == AppendResult::duplicateIgnored);
    KIRIN_PERCEPTUAL_REQUIRE (history.size() == 1u);
    KIRIN_PERCEPTUAL_REQUIRE (
        history.append (14'400, 48'000, 4'800, 1.1, 1.4, 0.3)
        == AppendResult::gapAppended);
    KIRIN_PERCEPTUAL_REQUIRE (history.size() == 2u);
    KIRIN_PERCEPTUAL_REQUIRE (! history.sampleAt (1u).continuesPrevious);
    KIRIN_PERCEPTUAL_REQUIRE (
        history.append (19'200, 48'000, 4'800, 1.1, 1.3, 0.2)
        == AppendResult::appended);
    KIRIN_PERCEPTUAL_REQUIRE (history.sampleAt (2u).continuesPrevious);
    KIRIN_PERCEPTUAL_REQUIRE (
        history.append (4'800, 48'000, 4'800, 1.0, 1.0, 0.0)
        == AppendResult::timelineReset);
    KIRIN_PERCEPTUAL_REQUIRE (history.size() == 1u);
    KIRIN_PERCEPTUAL_REQUIRE (
        history.append (9'600, 48'000, 4'799, 1.0, 1.0, 0.0)
        == AppendResult::rejected);

    history.clear();
    for (int index = 1; index <= 75; ++index)
        KIRIN_PERCEPTUAL_REQUIRE (
            history.append ((int64_t) index * 4'800, 48'000, 4'800,
                            1.0, 1.1, 0.1) == AppendResult::appended);
    KIRIN_PERCEPTUAL_REQUIRE (history.size() == historyCapacity);
    KIRIN_PERCEPTUAL_REQUIRE (history.sampleAt (0u).endpoint == 76'800);
    KIRIN_PERCEPTUAL_REQUIRE (std::abs (history.ageSecondsAt (0u) - 5.9) < 1.0e-12);

    PerceptualComponent component;
    component.setSnapshot (snapshot (4'800, 0.1));
    component.setSnapshot (snapshot (9'600, 0.2));
    KIRIN_PERCEPTUAL_REQUIRE (component.historySizeForTest() == 2u);
    auto newEpoch = snapshot (14'400, 0.3);
    newEpoch.state_epoch_samples = 9'600;
    component.setSnapshot (newEpoch);
    KIRIN_PERCEPTUAL_REQUIRE (component.historySizeForTest() == 1u);

    PerceptualComponent delayedUi;
    delayedUi.setBatch (batch (1, 1));
    delayedUi.setBatch (batch (1, 6));
    KIRIN_PERCEPTUAL_REQUIRE (delayedUi.historySizeForTest() == 6u);
    KIRIN_PERCEPTUAL_REQUIRE (! delayedUi.historyHasGapsForTest());
    KIRIN_PERCEPTUAL_REQUIRE (delayedUi.newestEndpointForTest() == 28'800);

    const auto curveCount = delayedUi.curvePresentationCountForTest();
    const auto numericCount = delayedUi.numericPresentationCountForTest();
    delayedUi.setBatch (batch (1, 7));
    const double now = juce::Time::getMillisecondCounterHiRes();
    delayedUi.presentationTickAt (now + 100.0);
    KIRIN_PERCEPTUAL_REQUIRE (
        delayedUi.curvePresentationCountForTest() == curveCount);
    KIRIN_PERCEPTUAL_REQUIRE (
        delayedUi.numericPresentationCountForTest() == numericCount);
    delayedUi.presentationTickAt (now + 210.0);
    KIRIN_PERCEPTUAL_REQUIRE (
        delayedUi.curvePresentationCountForTest() == curveCount + 1u);
    KIRIN_PERCEPTUAL_REQUIRE (
        delayedUi.numericPresentationCountForTest() == numericCount);
    delayedUi.presentationTickAt (now + 510.0);
    KIRIN_PERCEPTUAL_REQUIRE (
        delayedUi.numericPresentationCountForTest() == numericCount + 1u);

    delayedUi.setBatch (batch (1, 64));
    KIRIN_PERCEPTUAL_REQUIRE (delayedUi.historySizeForTest() == historyCapacity);
    KIRIN_PERCEPTUAL_REQUIRE (! delayedUi.historyHasGapsForTest());
    KIRIN_PERCEPTUAL_REQUIRE (delayedUi.newestEndpointForTest() == 307'200);

    delayedUi.setBatch (batch (70, 71));
    KIRIN_PERCEPTUAL_REQUIRE (delayedUi.historySizeForTest() == 2u);
    KIRIN_PERCEPTUAL_REQUIRE (! delayedUi.historyHasGapsForTest());
    KIRIN_PERCEPTUAL_REQUIRE (delayedUi.newestEndpointForTest() == 340'800);
}

void verifyPerceptualRenderingContract()
{
    static_assert (sizeof (KirinPerceptualView) == 56u);
    static_assert (sizeof (KirinPerceptualBatch) == 3'648u);
    verifyFillUsesZeroDistanceOnly();
    verifyHeldFillPersistsAfterPresentationStops();
    verifyUniformHistoryInk();
    const double compact = renderAt (
        ui_contract::spectrumSizePresets[0], "KIRIN_PERCEPTUAL_RENDER_OUTPUT");
    const double medium = renderAt (
        ui_contract::spectrumSizePresets[1], "KIRIN_PERCEPTUAL_RENDER_OUTPUT_MEDIUM");
    const double large = renderAt (
        ui_contract::spectrumSizePresets[2], "KIRIN_PERCEPTUAL_RENDER_OUTPUT_LARGE");
    std::cout << "Perceptual paint samples: " << compact << '/' << medium
              << '/' << large << " ms/frame\n";
    KIRIN_PERCEPTUAL_REQUIRE (compact < 2.0);
    KIRIN_PERCEPTUAL_REQUIRE (medium < 3.0);
    KIRIN_PERCEPTUAL_REQUIRE (large < 4.0);
}
}
