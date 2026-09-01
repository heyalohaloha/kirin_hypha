#include "TimeHistoryContractTest.h"

#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTimeAxisContract.h"

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
        const double correlation = 0.72 + wave * 0.16;
        const double plr = 12.0 + wave * 1.8;
        entry.correlation = { correlation - 0.03, correlation + 0.03, correlation };
        entry.plr = { plr - 0.2, plr + 0.2, plr };
    }
    return result;
}

juce::Image render (const std::vector<KirinMeterHistoryEntry>& history,
                    int width, int height, bool delta = false)
{
    observatory::View view (observatory::Role::post);
    view.setSize (width, height);
    view.setDomain (observatory::Domain::time);
    if (delta)
        view.setTarget (observatory::ObservationTarget::delta);
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
    KIRIN_TIME_HISTORY_REQUIRE (
        time_history::selectAxis (normal).mode == time_history::AxisMode::sessionWithDawRuns);
    auto dawAxisFixture = normal;
    dawAxisFixture.resize (3);
    for (auto& entry : dawAxisFixture)
        entry.run_id = 1u;
    dawAxisFixture.front().last_timeline_endpoint_samples = 0;
    dawAxisFixture[1].last_timeline_endpoint_samples = 10;
    dawAxisFixture.back().last_timeline_endpoint_samples = 100;
    const auto dawAxis = time_history::selectAxis (dawAxisFixture);
    KIRIN_TIME_HISTORY_REQUIRE (dawAxis.mode == time_history::AxisMode::daw);
    KIRIN_TIME_HISTORY_REQUIRE (
        std::abs (time_history::normalizedX (dawAxis, dawAxisFixture[1], 1,
                                             dawAxisFixture.size()) - 0.1) < 1.0e-12);
    const auto alternate = fixture (true);
    const auto normalImage = render (normal, 600, 400);
    const auto alternateImage = render (alternate, 600, 400);
    KIRIN_TIME_HISTORY_REQUIRE (changedPixels (normalImage, alternateImage) > 1'000);

    auto difference = fixture (false);
    for (size_t index = 0u; index < difference.size(); ++index)
    {
        const double value = std::sin ((double) index * 0.115) * 4.5;
        difference[index].lufs_m = { value - 0.4, value + 0.4, value };
        difference[index].lufs_s = { value * 0.6 - 0.2, value * 0.6 + 0.2, value * 0.6 };
        difference[index].true_peak = { -value - 0.3, -value + 0.3, -value };
        difference[index].correlation = { value * 0.08 - 0.03,
                                          value * 0.08 + 0.03,
                                          value * 0.08 };
        difference[index].plr = { value * 0.4 - 0.2,
                                  value * 0.4 + 0.2,
                                  value * 0.4 };
    }
    const auto differenceImage = render (difference, 600, 400, true);
    KIRIN_TIME_HISTORY_REQUIRE (changedPixels (normalImage, differenceImage) > 1'000);

    auto alternateAux = normal;
    for (auto& entry : alternateAux)
    {
        entry.plr.mean += 4.0;
        entry.correlation.mean -= 0.6;
    }

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
        const auto auxChanged = changedPixels (
            image, render (alternateAux, dimensions.first, dimensions.second));
        const auto compact = dimensions.first <= 375;
        KIRIN_TIME_HISTORY_REQUIRE (compact ? auxChanged == 0 : auxChanged > 30);
    }

    constexpr int paintIterations = 30;
    const double startedMs = juce::Time::getMillisecondCounterHiRes();
    for (int index = 0; index < paintIterations; ++index)
        render (normal, 600, 400);
    const double paintMs = (juce::Time::getMillisecondCounterHiRes() - startedMs)
                         / paintIterations;
    std::cout << "TIME five-fact paint: " << paintMs << " ms/frame\n";
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
    const auto deltaOutputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_TIME_DELTA_TEST_PNG", {});
    if (deltaOutputPath.isNotEmpty())
    {
        auto output = juce::File (deltaOutputPath).createOutputStream();
        KIRIN_TIME_HISTORY_REQUIRE (output != nullptr);
        KIRIN_TIME_HISTORY_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (differenceImage, *output));
    }
}
}
