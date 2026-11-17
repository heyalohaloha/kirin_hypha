#pragma once
#include "kirin_hypha_reference_metrics.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace hypha::reference_audition::metrics
{
// These are measured facts of the un-MATCHed played copy, not OS analysis values.
// Summaries are prepared off the message/audio threads; no sorting during paint.
enum class Metric : std::uint8_t { psr, crest, sharp };
constexpr double missing = std::numeric_limits<double>::quiet_NaN();
inline double value (const KirinReferenceMetricFrame& frame, Metric metric) noexcept
{
    switch (metric)
    {
        case Metric::psr: return frame.psr_db;
        case Metric::crest: return frame.crest_db;
        case Metric::sharp: return frame.sharpness_acum;
    }
    return missing;
}
struct Cue
{
    std::uint64_t start = 0, end = 0; // host-rate frames; half-open left, closed endpoint on right
    bool containsEndpoint (std::uint64_t sample) const noexcept
    { return end > start && sample > start && sample <= end; }
};
struct Summary
{
    double median = missing;
    std::size_t observations = 0;
    bool complete = false; // requested PCM scan succeeded; legitimate missing measurements stay missing
};
inline Summary summarise (const std::vector<KirinReferenceMetricFrame>& frames, Metric metric, const std::vector<Cue>& regions, bool complete = false)
{
    std::vector<double> values;
    values.reserve (frames.size());
    for (const auto& frame : frames)
        if (std::any_of (regions.begin(), regions.end(), [&] (const auto& cue) { return cue.containsEndpoint (frame.end_sample); }))
            if (const auto measured = value (frame, metric); std::isfinite (measured))
                values.push_back (measured);
    if (values.empty()) return { missing, 0, complete };
    std::sort (values.begin(), values.end());
    const auto middle = values.size() / 2;
    const auto median = values.size() % 2 != 0 ? values[middle]
        : values[middle - 1] * 0.5 + values[middle] * 0.5;
    return { median, values.size(), complete };
}
inline Summary summarise (const std::vector<KirinReferenceMetricFrame>& frames, Metric metric, Cue cue, bool complete = false)
{ return summarise (frames, metric, std::vector<Cue> { cue }, complete); }
struct Band { double low = missing, high = missing; };
// Every selected song must be complete. Missing is not a zero and not permission to
// narrow the set to the songs that happened to finish first.
inline Band setBand (const std::vector<std::optional<Summary>>& songs)
{
    if (songs.size() < 2) return {};
    std::vector<double> values;
    for (const auto& song : songs)
    {
        if (!song || !song->complete || song->observations == 0 || !std::isfinite (song->median)) return {};
        values.push_back (song->median);
    }
    std::sort (values.begin(), values.end());
    const auto rank = [&] (double fraction) {
        const auto count = static_cast<std::size_t> (std::ceil (fraction * static_cast<double> (values.size())));
        return values[std::clamp<std::size_t> (count, 1, values.size()) - 1];
    };
    return { rank (0.1), rank (0.9) };
}
// The most recently completed exact aperture at this position. No interpolation of
// peaks or SHARP, no extrapolation outside the source, and no reuse over a missing point.
inline double atPosition (const std::vector<KirinReferenceMetricFrame>& frames, Metric metric,
                          std::uint64_t sample, std::uint64_t sourceLength,
                          std::uint32_t aperture) noexcept
{
    if (frames.empty() || aperture == 0 || sample >= sourceLength || sample < aperture) return missing;
    const auto end = sample / aperture * aperture;
    const auto found = std::lower_bound (frames.begin(), frames.end(), end,
        [] (const auto& frame, std::uint64_t endpoint) { return frame.end_sample < endpoint; });
    if (found == frames.end() || found->end_sample != end) return missing;
    return value (*found, metric);
}
}
