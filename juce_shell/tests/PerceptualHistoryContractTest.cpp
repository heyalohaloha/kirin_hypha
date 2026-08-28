#include "PerceptualHistoryContractTest.h"

#include "../src/HyphaPerceptualComponent.h"
#include "../src/HyphaPerceptualHistory.h"
#include "../src/HyphaUiContract.h"

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
}

void verifyPerceptualRenderingContract()
{
    static_assert (sizeof (KirinPerceptualView) == 48u);
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
