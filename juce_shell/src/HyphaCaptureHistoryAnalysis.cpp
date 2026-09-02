#include "HyphaCaptureHistoryPainter.h"

#include "HyphaTheme.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace hypha::capture_history
{
namespace
{
bool sameRun (const KirinMeterHistoryEntry& first,
              const KirinMeterHistoryEntry& second) noexcept
{
    return first.generation == second.generation && first.run_id == second.run_id;
}

double secondsBeforeEnd (const std::vector<KirinMeterHistoryEntry>& history,
                         std::size_t index,
                         double sampleRate) noexcept
{
    if (history.empty() || index >= history.size() || ! std::isfinite (sampleRate)
        || sampleRate <= 0.0)
        return 0.0;
    const auto latest = history.back().last_observed_frames;
    const auto observed = history[index].last_observed_frames;
    return observed <= latest ? static_cast<double> (latest - observed) / sampleRate : 0.0;
}
}

void retainThrough (std::vector<KirinMeterHistoryEntry>& history,
                    std::uint64_t observedFrames)
{
    if (observedFrames == 0u)
    {
        history.clear();
        return;
    }
    history.erase (std::remove_if (history.begin(), history.end(), [observedFrames] (const auto& entry)
    {
        return entry.last_observed_frames > observedFrames;
    }), history.end());
}

TruePeakSummary analyseTruePeak (const std::vector<KirinMeterHistoryEntry>& history,
                                 double sampleRate)
{
    TruePeakSummary result;
    auto maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto value = history[index].true_peak.max;
        if (std::isfinite (value) && value > maximum)
        {
            maximum = value;
            result.available = true;
            result.windowMaximumDbtp = value;
            result.windowMaximumIndex = index;
        }
    }
    if (! result.available)
        return result;

    std::optional<std::size_t> excursionMaximum;
    const auto closeExcursion = [&]
    {
        if (excursionMaximum.has_value())
            result.eventIndices.push_back (*excursionMaximum);
        excursionMaximum.reset();
    };
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto value = history[index].true_peak.max;
        if (! std::isfinite (value) || ! hypha::tpOver (value))
        {
            closeExcursion();
            continue;
        }
        if (excursionMaximum.has_value()
            && ! sameRun (history[*excursionMaximum], history[index]))
            closeExcursion();
        if (! excursionMaximum.has_value()
            || value > history[*excursionMaximum].true_peak.max)
            excursionMaximum = index;
    }
    closeExcursion();
    if (std::find (result.eventIndices.begin(), result.eventIndices.end(),
                   result.windowMaximumIndex) == result.eventIndices.end())
        result.eventIndices.push_back (result.windowMaximumIndex);
    std::sort (result.eventIndices.begin(), result.eventIndices.end());
    result.secondsBeforeEnd = secondsBeforeEnd (
        history, result.windowMaximumIndex, sampleRate);
    return result;
}
}
