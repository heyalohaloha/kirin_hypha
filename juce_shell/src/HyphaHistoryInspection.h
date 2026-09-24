#pragma once

#include "HyphaCaptureHistoryPainter.h"
#include <cmath>
#include <functional>
#include <limits>

namespace hypha::history_inspection
{
inline bool strongPeak (double value) noexcept { return std::isfinite (value) && value > 0.0; }
inline juce::String peakText (double value)
{
    if (! std::isfinite (value)) return "---";
    if (value > 0.0 && value < 0.005) return "+<0.01";
    if (value < 0.0 && value > -0.005) return "-<0.01";
    return (value > 0.0 ? "+" : "") + juce::String (value, 2);
}

// This ABI retains the endpoint but not ProjectTimeline vs AudioRenderTimeline provenance.
// Never label it DAW/project time. It is a host-clock TP-window endpoint, not an exact peak.
inline juce::String positionText (const KirinMeterHistoryEntry& entry, double rate)
{
    if (! std::isfinite (rate) || rate <= 0.0) return "POSITION UNAVAILABLE";
    const auto timeline = entry.last_timeline_endpoint_samples;
    const bool known = timeline != std::numeric_limits<std::int64_t>::min();
    const double seconds = known ? static_cast<double> (timeline) / rate
                                 : static_cast<double> (entry.last_observed_frames) / rate;
    if (! std::isfinite (seconds) || std::abs (seconds) > 86400.0 * 365.0)
        return "POSITION UNAVAILABLE";
    const auto ms = static_cast<std::int64_t> (std::llround (std::abs (seconds) * 1000.0));
    return juce::String (known ? "HOST ~" : "ELAPSED ") + (seconds < 0 ? "-" : "")
        + juce::String (ms / 60000).paddedLeft ('0', 2) + ":"
        + juce::String ((ms / 1000) % 60).paddedLeft ('0', 2) + "."
        + juce::String (ms % 1000).paddedLeft ('0', 3);
}

struct Selection
{
    std::vector<KirinMeterHistoryEntry> snapshot;
    std::optional<std::size_t> index;
    double sampleRate = 0.0;
    std::uint64_t epoch = 0, generation = 0;

    bool held() const noexcept { return index.has_value() && *index < snapshot.size(); }
    void clear() { snapshot.clear(); index.reset(); }
    bool matches (const KirinMeterSession& meter) const noexcept
    {
        return epoch == meter.measurement_epoch && generation == meter.generation
            && std::equal_to<double> {} (sampleRate, meter.sample_rate)
            && meter.state != KIRIN_METER_SESSION_EMPTY;
    }
    bool pin (const std::vector<KirinMeterHistoryEntry>& live, std::size_t at,
              const KirinMeterSession& meter)
    {
        if (at >= live.size() || ! std::isfinite (meter.sample_rate) || meter.sample_rate <= 0) return false;
        if (meter.state == KIRIN_METER_SESSION_EMPTY
            || live.back().measurement_epoch != meter.measurement_epoch
            || live.back().generation != meter.generation) return false;
        snapshot = live; index = at; sampleRate = meter.sample_rate;
        epoch = meter.measurement_epoch; generation = meter.generation;
        return true;
    }
    std::optional<std::size_t> event (const std::vector<KirinMeterHistoryEntry>& live,
                                     double rate, int direction) const
    {
        const auto& entries = held() ? snapshot : live;
        const auto peaks = capture_history::analyseTruePeak (entries, held() ? sampleRate : rate);
        if (peaks.eventIndices.empty()) return std::nullopt;
        if (! held()) return direction < 0 ? peaks.eventIndices.back() : peaks.eventIndices.front();
        if (direction < 0)
        {
            for (auto it = peaks.eventIndices.rbegin(); it != peaks.eventIndices.rend(); ++it)
                if (*it < *index) return *it;
        }
        else
            for (auto at : peaks.eventIndices) if (at > *index) return at;
        return std::nullopt;
    }
};
}
