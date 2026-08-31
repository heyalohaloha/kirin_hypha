#include "TimeHistoryContractTest.h"

#include "../src/HyphaObservatoryView.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "TIME history contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_TIME_HISTORY_REQUIRE(expression) require ((expression), #expression, __LINE__)

std::vector<KirinMeterHistoryEntry> fixture (bool alternate)
{
    std::vector<KirinMeterHistoryEntry> result (180);
    for (size_t index = 0u; index < result.size(); ++index)
    {
        auto& entry = result[index];
        entry.generation = 9;
        entry.run_id = index < 92u ? 1u : 2u;
        entry.first_observed_frames = index * 4'800u;
        entry.last_observed_frames = entry.first_observed_frames + 4'799u;
        entry.first_timeline_endpoint_samples = static_cast<int64_t> (
            entry.first_observed_frames);
        entry.last_timeline_endpoint_samples = static_cast<int64_t> (
            entry.last_observed_frames);
        entry.observation_count = index % 3u == 0u ? 10u : 1u;
        entry.resolution = entry.observation_count > 1u
            ? KIRIN_METER_HISTORY_1_HZ : KIRIN_METER_HISTORY_10_HZ;
        const double wave = std::sin ((double) index * 0.115);
        const double momentary = -22.0 + wave * 5.0;
        const double shortTerm = alternate ? -34.0 + wave : -19.0 + wave * 2.0;
        const double peak = alternate ? -16.0 + wave : -4.0 + wave * 1.5;
        entry.lufs_m = { momentary - 0.7, momentary + 0.7, momentary };
        entry.lufs_s = { shortTerm - 0.3, shortTerm + 0.3, shortTerm };
        entry.true_peak = { peak - 0.4, peak + 0.4, peak };
        entry.correlation = { 0.8, 0.8, 0.8 };
    }
    return result;
}

juce::Image render (const std::vector<KirinMeterHistoryEntry>& history, int width, int height)
{
    observatory::View view (observatory::Role::post);
    view.setSize (width, height);
    view.setDomain (observatory::Domain::time);
    view.setHistory (history);
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

int changedPixels (const juce::Image& left, const juce::Image& right)
{
    int count = 0;
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            count += left.getPixelAt (x, y).getARGB() != right.getPixelAt (x, y).getARGB();
    return count;
}
}

void verifyTimeHistoryContract()
{
    const auto normal = fixture (false);
    const auto alternate = fixture (true);
    const auto normalImage = render (normal, 600, 400);
    const auto alternateImage = render (alternate, 600, 400);
    KIRIN_TIME_HISTORY_REQUIRE (changedPixels (normalImage, alternateImage) > 1'000);

    for (const auto dimensions : {
             std::pair { 300, 200 }, std::pair { 375, 250 },
             std::pair { 450, 300 }, std::pair { 600, 400 } })
    {
        const auto image = render (normal, dimensions.first, dimensions.second);
        int visible = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                visible += image.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
        KIRIN_TIME_HISTORY_REQUIRE (visible == image.getWidth() * image.getHeight());
    }

    constexpr int paintIterations = 30;
    const double startedMs = juce::Time::getMillisecondCounterHiRes();
    for (int index = 0; index < paintIterations; ++index)
        render (normal, 600, 400);
    const double paintMs = (juce::Time::getMillisecondCounterHiRes() - startedMs)
                         / paintIterations;
    std::cout << "TIME three-fact paint: " << paintMs << " ms/frame\n";
    KIRIN_TIME_HISTORY_REQUIRE (paintMs < 12.0);

    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_TIME_TEST_PNG", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_TIME_HISTORY_REQUIRE (output != nullptr);
        KIRIN_TIME_HISTORY_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (normalImage, *output));
    }
}
}
