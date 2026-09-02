#include "CaptureHistoryContractTest.h"

#include "../src/HyphaCaptureHistoryPainter.h"

#include <algorithm>
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
    std::cerr << "Capture History contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_CAPTURE_HISTORY_REQUIRE(expression) require ((expression), #expression, __LINE__)

std::vector<KirinMeterHistoryEntry> fixture()
{
    constexpr double peaks[] { -8.0, -2.0, -7.0, -9.0, -5.0, -0.8, -6.0 };
    std::vector<KirinMeterHistoryEntry> result (sizeof (peaks) / sizeof (peaks[0]));
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        auto& entry = result[index];
        entry.generation = 4;
        entry.run_id = index < 4u ? 1u : 2u;
        entry.first_observed_frames = (index + 1u) * 4'800u;
        entry.last_observed_frames = entry.first_observed_frames;
        entry.first_timeline_endpoint_samples = static_cast<std::int64_t> (
            entry.first_observed_frames);
        entry.last_timeline_endpoint_samples = entry.first_timeline_endpoint_samples;
        entry.observation_count = 1;
        entry.resolution = KIRIN_METER_HISTORY_10_HZ;
        entry.clip_event_count[0] = index == 1u ? 1u : 0u;
        entry.clip_event_count[1] = index == 5u ? 2u : 0u;
        const auto loudness = -24.0 + std::sin (static_cast<double> (index)) * 3.0;
        entry.lufs_m = { loudness, loudness, loudness };
        entry.lufs_s = { loudness + 2.0, loudness + 2.0, loudness + 2.0 };
        entry.true_peak = { peaks[index] - 0.5, peaks[index], peaks[index] - 2.0 };
    }
    return result;
}

juce::Image render (const std::vector<KirinMeterHistoryEntry>& history,
                    bool delta,
                    std::optional<std::size_t> hovered = std::nullopt)
{
    juce::Image image (juce::Image::ARGB, 500, 130, true);
    juce::Graphics graphics (image);
    capture_history::paint (graphics, image.getBounds(), history, delta, 48'000.0, hovered);
    return image;
}

int changedPixels (const juce::Image& first, const juce::Image& second)
{
    KIRIN_CAPTURE_HISTORY_REQUIRE (first.getBounds() == second.getBounds());
    int changed = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            changed += first.getPixelAt (x, y).getARGB()
                    != second.getPixelAt (x, y).getARGB();
    return changed;
}
}

void verifyCaptureHistoryContract()
{
    static_assert (capture_history::normalizedLoudness (-36.0, false) == 0.0);
    static_assert (capture_history::normalizedLoudness (-18.0, false) == 0.5);
    static_assert (capture_history::normalizedLoudness (0.0, false) == 1.0);
    static_assert (capture_history::normalizedLoudness (-48.0, false) == 0.0);
    const auto history = fixture();
    const auto summary = capture_history::analyseTruePeak (history, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (summary.available);
    KIRIN_CAPTURE_HISTORY_REQUIRE (summary.windowMaximumIndex == 5u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (std::abs (summary.windowMaximumDbtp - -0.8) < 1.0e-12);
    KIRIN_CAPTURE_HISTORY_REQUIRE (std::abs (summary.secondsBeforeEnd - 0.1) < 1.0e-12);
    KIRIN_CAPTURE_HISTORY_REQUIRE (summary.eventIndices.size() == 1u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (summary.eventIndices.front() == 5u);
    auto twoEmphasisExcursions = history;
    twoEmphasisExcursions[1].true_peak.max = -0.5;
    const auto emphasized = capture_history::analyseTruePeak (
        twoEmphasisExcursions, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (emphasized.eventIndices.size() == 2u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (emphasized.eventIndices[0] == 1u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (emphasized.eventIndices[1] == 5u);

    auto noPeakFacts = history;
    for (auto& entry : noPeakFacts)
        entry.true_peak = { std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::quiet_NaN() };
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        ! capture_history::analyseTruePeak (noPeakFacts, 48'000.0).available);
    const auto zeroRate = capture_history::analyseTruePeak (history, 0.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (zeroRate.available);
    KIRIN_CAPTURE_HISTORY_REQUIRE (zeroRate.secondsBeforeEnd == 0.0);

    const auto area = juce::Rectangle<int> (0, 0, 500, 130);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        ! capture_history::hitTest (
            area, history, { 250.0f, 10.0f }, 48'000.0).has_value());
    const auto beforeAvailableWindow = capture_history::hitTest (
        area, history, { 250.0f, 65.0f }, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (! beforeAvailableWindow.has_value());
    const auto newest = capture_history::hitTest (
        area, history, { 459.9f, 65.0f }, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (newest.has_value());
    KIRIN_CAPTURE_HISTORY_REQUIRE (*newest == history.size() - 1u);

    const auto factual = render (history, false);
    const auto missing = render (noPeakFacts, false);
    auto noClipFacts = history;
    for (auto& entry : noClipFacts)
        entry.clip_event_count[0] = entry.clip_event_count[1] = 0u;
    KIRIN_CAPTURE_HISTORY_REQUIRE (changedPixels (factual, missing) > 40);
    KIRIN_CAPTURE_HISTORY_REQUIRE (changedPixels (factual, render (noClipFacts, false)) > 10);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (factual, render (history, false, 5u)) > 40);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (render (history, true), render (noPeakFacts, true)) == 0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (render (history, true), render (noClipFacts, true)) == 0);

    auto frozen = history;
    capture_history::retainThrough (frozen, history[4].last_observed_frames);
    KIRIN_CAPTURE_HISTORY_REQUIRE (frozen.size() == 5u);
    capture_history::retainThrough (frozen, 0u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (frozen.empty());
}
}
