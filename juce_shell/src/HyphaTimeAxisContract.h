#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "kirin_hypha_ffi.h"

namespace hypha::time_history
{
enum class AxisMode
{
    session,
    daw,
    sessionWithDawRuns,
};

struct HistoryAxis
{
    AxisMode mode = AxisMode::session;
    std::uint64_t firstObserved = 0;
    std::uint64_t lastObserved = 0;
    std::int64_t firstDaw = 0;
    std::int64_t lastDaw = 0;
};

inline HistoryAxis selectAxis (const std::vector<KirinMeterHistoryEntry>& history) noexcept
{
    if (history.empty())
        return {};
    HistoryAxis axis;
    axis.firstObserved = history.front().last_observed_frames;
    axis.lastObserved = history.back().last_observed_frames;
    constexpr auto unavailable = std::numeric_limits<std::int64_t>::min();
    const auto generation = history.front().generation;
    const auto run = history.front().run_id;
    bool allDaw = true;
    bool oneRun = true;
    bool monotonicDaw = true;
    auto previousDaw = history.front().last_timeline_endpoint_samples;
    for (const auto& entry : history)
    {
        allDaw &= entry.last_timeline_endpoint_samples != unavailable;
        oneRun &= entry.generation == generation && entry.run_id == run;
        if (entry.last_timeline_endpoint_samples != unavailable && previousDaw != unavailable)
            monotonicDaw &= entry.last_timeline_endpoint_samples >= previousDaw;
        previousDaw = entry.last_timeline_endpoint_samples;
    }
    if (allDaw && oneRun && monotonicDaw)
    {
        axis.mode = AxisMode::daw;
        axis.firstDaw = history.front().last_timeline_endpoint_samples;
        axis.lastDaw = history.back().last_timeline_endpoint_samples;
    }
    else if (allDaw)
    {
        axis.mode = AxisMode::sessionWithDawRuns;
    }
    return axis;
}

inline double normalizedX (const HistoryAxis& axis,
                           const KirinMeterHistoryEntry& entry,
                           std::size_t index,
                           std::size_t count) noexcept
{
    if (axis.mode == AxisMode::daw && axis.lastDaw > axis.firstDaw
        && entry.last_timeline_endpoint_samples >= axis.firstDaw)
        return static_cast<double> (entry.last_timeline_endpoint_samples - axis.firstDaw)
             / static_cast<double> (axis.lastDaw - axis.firstDaw);
    if (axis.lastObserved > axis.firstObserved
        && entry.last_observed_frames >= axis.firstObserved)
        return static_cast<double> (entry.last_observed_frames - axis.firstObserved)
             / static_cast<double> (axis.lastObserved - axis.firstObserved);
    return count > 1u ? static_cast<double> (index) / static_cast<double> (count - 1u) : 1.0;
}

inline const char* axisLabel (AxisMode mode) noexcept
{
    if (mode == AxisMode::daw)
        return "DAW";
    if (mode == AxisMode::sessionWithDawRuns)
        return "SESSION + DAW RUNS";
    return "SESSION";
}
}
