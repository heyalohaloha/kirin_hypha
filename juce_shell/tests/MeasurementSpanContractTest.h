#pragma once

#include "../src/HyphaRunSummary.h"

#include <cstdlib>
#include <iostream>
#include <vector>

/**
    Measurement spans do not merge (P-3 / D-12).

    `generation` and `run_id` both restart at 1 when a new engine is built, so the first rows of a
    new span carry the same numbers as the first rows of the old one. Only `measurement_epoch`
    separates them. Pure data, so it runs before the font-dependent contracts.
*/
namespace hypha::tests::measurement_span_contract
{
inline void require (bool condition, const char* what, int line)
{
    if (condition)
        return;
    std::cerr << "Measurement span contract failed at line " << line << ": " << what << '\n';
    std::exit (EXIT_FAILURE);
}
#define KIRIN_SPAN_REQUIRE(expr) require ((expr), #expr, __LINE__)

inline KirinMeterHistoryEntry row (std::uint64_t epoch, std::uint64_t observed, double momentary)
{
    KirinMeterHistoryEntry entry {};
    entry.measurement_epoch = epoch;
    entry.generation = 1; // the value a fresh engine always starts from
    entry.run_id = 1;
    entry.first_observed_frames = observed;
    entry.last_observed_frames = observed;
    entry.first_timeline_endpoint_samples = static_cast<std::int64_t> (observed);
    entry.last_timeline_endpoint_samples = static_cast<std::int64_t> (observed);
    entry.observation_count = 1;
    entry.resolution = KIRIN_METER_HISTORY_10_HZ;
    entry.lufs_m = { momentary, momentary, momentary };
    entry.lufs_s = { momentary, momentary, momentary };
    entry.true_peak = { momentary, momentary, momentary };
    entry.correlation = { 0.8, 0.8, 0.8 };
    entry.plr = { 10.0, 10.0, 10.0 };
    return entry;
}

inline void verify()
{
    // Identical generation and run_id, different spans. Merging them would average a measurement
    // made under one layout into a run reported for another.
    const std::vector<KirinMeterHistoryEntry> twoSpans { row (7, 4'800, -20.0),
                                                         row (8, 9'600, -10.0) };
    const auto split = run_summary::summarize (twoSpans);
    KIRIN_SPAN_REQUIRE (split.runs.size() == 2u);
    KIRIN_SPAN_REQUIRE (split.runs[0].measurementEpoch == 7u);
    KIRIN_SPAN_REQUIRE (split.runs[1].measurementEpoch == 8u);
    KIRIN_SPAN_REQUIRE (split.runs[0].observationCount == 1u);

    // The same span still groups, so the split is the epoch and not an accident of the rows.
    const std::vector<KirinMeterHistoryEntry> oneSpan { row (7, 4'800, -20.0),
                                                       row (7, 9'600, -10.0) };
    const auto grouped = run_summary::summarize (oneSpan);
    KIRIN_SPAN_REQUIRE (grouped.runs.size() == 1u);
    KIRIN_SPAN_REQUIRE (grouped.runs[0].observationCount == 2u);

    std::cout << "Measurement span: PASS (a new engine's run does not continue the previous one)"
              << '\n';
}
#undef KIRIN_SPAN_REQUIRE

} // namespace hypha::tests::measurement_span_contract
