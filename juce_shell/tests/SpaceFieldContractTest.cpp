#include "SpaceFieldContractTest.h"

#include "../src/HyphaObservatoryView.h"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "SPACE field contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_SPACE_REQUIRE(expression) require ((expression), #expression, __LINE__)

KirinMeterSession fixture (bool sideDominant)
{
    KirinMeterSession meter {};
    meter.generation = 12;
    meter.active_frames = 48'000u * 18u;
    meter.observed_frames = meter.active_frames;
    meter.sample_rate = 48'000;
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.lufs_m = -17.0;
    meter.channels = 2;
    meter.balance_state = KIRIN_BALANCE_NUMERIC;
    meter.balance_db = sideDominant ? -1.4 : 0.6;
    meter.correlation = sideDominant ? -0.72 : 0.94;
    meter.field_size = KIRIN_STEREO_FIELD_SIZE;
    meter.field_observation_count = 30;
    constexpr size_t centre = KIRIN_STEREO_FIELD_SIZE / 2u;
    for (size_t offset = 2u; offset < KIRIN_STEREO_FIELD_SIZE - 2u; ++offset)
    {
        const size_t row = sideDominant ? centre : offset;
        const size_t column = sideDominant ? offset : centre;
        const int distance = std::abs ((int) offset - (int) centre);
        meter.field_density[row * KIRIN_STEREO_FIELD_SIZE + column]
            = static_cast<uint8_t> (juce::jmax (36, 255 - distance * 17));
    }
    return meter;
}

juce::Image render (const KirinMeterSession& meter, int width, int height)
{
    observatory::View view (observatory::Role::post);
    view.setSize (width, height);
    view.setDomain (observatory::Domain::space);
    view.setMeterSnapshot (meter, true);
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

int changedPixels (const juce::Image& first, const juce::Image& second)
{
    KIRIN_SPACE_REQUIRE (first.getBounds() == second.getBounds());
    int count = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            count += first.getPixelAt (x, y).getARGB()
                  != second.getPixelAt (x, y).getARGB();
    return count;
}
}

void verifySpaceFieldContract()
{
    const auto mid = fixture (false);
    const auto side = fixture (true);
    const auto midImage = render (mid, 600, 400);
    const auto sideImage = render (side, 600, 400);
    KIRIN_SPACE_REQUIRE (changedPixels (midImage, sideImage) > 500);
    const auto outputDirectory = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_SPACE_TEST_DIR", {});

    for (const auto dimensions : {
             std::pair { 300, 200 }, std::pair { 375, 250 },
             std::pair { 450, 300 }, std::pair { 600, 400 },
             std::pair { 900, 600 } })
    {
        const auto image = render (mid, dimensions.first, dimensions.second);
        int visible = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                visible += image.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
        KIRIN_SPACE_REQUIRE (visible == image.getWidth() * image.getHeight());
        if (outputDirectory.isNotEmpty())
        {
            const auto directory = juce::File (outputDirectory);
            KIRIN_SPACE_REQUIRE (directory.createDirectory().wasOk());
            const auto file = directory.getChildFile (
                "hypha-space-" + juce::String (dimensions.first) + "x"
                + juce::String (dimensions.second) + ".png");
            auto output = file.createOutputStream();
            KIRIN_SPACE_REQUIRE (output != nullptr);
            KIRIN_SPACE_REQUIRE (
                juce::PNGImageFormat().writeImageToStream (image, *output));
        }
    }

    auto mono = mid;
    mono.channels = 1;
    mono.field_size = 0;
    mono.field_observation_count = 0;
    KIRIN_SPACE_REQUIRE (changedPixels (midImage, render (mono, 600, 400)) > 300);

    constexpr int paintIterations = 30;
    const double startedMs = juce::Time::getMillisecondCounterHiRes();
    for (int index = 0; index < paintIterations; ++index)
        render (mid, 600, 400);
    const double paintMs = (juce::Time::getMillisecondCounterHiRes() - startedMs)
                         / paintIterations;
    std::cout << "SPACE density paint: " << paintMs << " ms/frame\n";
    KIRIN_SPACE_REQUIRE (paintMs < 12.0);

    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_SPACE_TEST_PNG", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_SPACE_REQUIRE (output != nullptr);
        KIRIN_SPACE_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (midImage, *output));
    }
}
}
