#include "RunSummaryContractTest.h"

#include "../src/HyphaRunSummary.h"
#include "../src/HyphaTheme.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "RUN summary contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}
#define KIRIN_RUN_REQUIRE(expression) require ((expression), #expression, __LINE__)

KirinMeterHistoryEntry point (std::uint64_t generation, std::uint64_t run,
                              std::uint64_t observed, double momentary,
                              double peak, std::uint16_t count = 1)
{
    KirinMeterHistoryEntry entry {};
    entry.generation = generation;
    entry.run_id = run;
    entry.first_observed_frames = observed;
    entry.last_observed_frames = observed + 4'799u;
    entry.first_timeline_endpoint_samples = static_cast<std::int64_t> (observed);
    entry.last_timeline_endpoint_samples = static_cast<std::int64_t> (observed + 4'799u);
    entry.observation_count = count;
    entry.resolution = KIRIN_METER_HISTORY_10_HZ;
    entry.lufs_m = { momentary - 1.0, momentary + 1.0, momentary };
    entry.lufs_s = { momentary - 0.5, momentary + 0.5, momentary };
    entry.true_peak = { peak - 1.0, peak, peak - 0.5 };
    entry.correlation = { 0.7, 0.9, 0.8 };
    entry.plr = { 9.0, 11.0, 10.0 };
    return entry;
}
}

void verifyRunSummaryContract()
{
    std::vector<KirinMeterHistoryEntry> history {
        point (4, 1, 0, -20.0, -4.0, 1),
        point (4, 1, 4'800, -10.0, -2.0, 3),
        point (4, 2, 9'600, -16.0, -1.0, 1),
        point (5, 1, 14'400, -14.0, -3.0, 1),
    };
    history[1].clip_event_count[0] = 2;
    history[2].clip_event_count[1] = 1;
    const auto result = run_summary::summarize (history);
    KIRIN_RUN_REQUIRE (result.exactTimeline);
    KIRIN_RUN_REQUIRE (result.runs.size() == 3u);
    KIRIN_RUN_REQUIRE (result.runs[0].generation == 4u);
    KIRIN_RUN_REQUIRE (result.runs[2].generation == 5u);
    KIRIN_RUN_REQUIRE (result.runs[0].observationCount == 4u);
    KIRIN_RUN_REQUIRE (result.runs[0].clipEvents[0] == 2u);
    KIRIN_RUN_REQUIRE (std::abs (result.runs[0].momentary.mean - -12.5) < 1.0e-12);
    KIRIN_RUN_REQUIRE (std::abs (result.runs[0].momentary.minimum - -21.0) < 1.0e-12);
    KIRIN_RUN_REQUIRE (std::abs (result.runs[0].momentary.maximum - -9.0) < 1.0e-12);
    KIRIN_RUN_REQUIRE (std::abs (result.runs[0].maximumTruePeak - -2.0) < 1.0e-12);

    auto unknown = history;
    for (auto& entry : unknown)
    {
        entry.generation = 4;
        entry.run_id = 1;
        entry.first_timeline_endpoint_samples = std::numeric_limits<std::int64_t>::min();
        entry.last_timeline_endpoint_samples = std::numeric_limits<std::int64_t>::min();
    }
    const auto unknownResult = run_summary::summarize (unknown);
    KIRIN_RUN_REQUIRE (unknownResult.available());
    KIRIN_RUN_REQUIRE (! unknownResult.exactTimeline);
    KIRIN_RUN_REQUIRE (unknownResult.runs.size() == 1u);
    auto incompleteClock = history;
    incompleteClock[0].first_timeline_endpoint_samples = std::numeric_limits<std::int64_t>::min();
    KIRIN_RUN_REQUIRE (! run_summary::summarize (incompleteClock).available());
    auto mixedResolution = history;
    mixedResolution[2].resolution = KIRIN_METER_HISTORY_1_HZ;
    KIRIN_RUN_REQUIRE (! run_summary::summarize (mixedResolution).exactTimeline);
    auto backwards = history;
    backwards[1].first_timeline_endpoint_samples = -1;
    KIRIN_RUN_REQUIRE (! run_summary::summarize (backwards).exactTimeline);
    KIRIN_RUN_REQUIRE (run_summary::visibleRowCount (300) == 1);
    KIRIN_RUN_REQUIRE (run_summary::visibleRowCount (600) == 3);
    KIRIN_RUN_REQUIRE (run_summary::visibleRowCount (900) == 6);

    juce::Image image (juce::Image::ARGB, 600, 300, true);
    image.clear (image.getBounds(), BG);
    juce::Graphics graphics (image);
    run_summary::paint (graphics, image.getBounds(), result, 48'000.0);
    int changed = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            changed += image.getPixelAt (x, y).getARGB() != BG.getARGB();
    KIRIN_RUN_REQUIRE (changed > 1'000);
}
}
