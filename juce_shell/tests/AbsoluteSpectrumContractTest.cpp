#include "AbsoluteSpectrumContractTest.h"

#include "../src/HyphaAbsoluteSpectrumHistory.h"
#include "../src/HyphaSpectrumComponent.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
}

void verifyAbsoluteSpectrumContract()
{
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
