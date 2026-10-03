#include "ReferenceLiveWindowLoudness.h"

#include <algorithm>
#include <cmath>

namespace hypha::reference_audition
{
namespace
{
constexpr double absoluteGateLufs = -70.0;
constexpr double relativeGateLu = -10.0;

double meanEnergyLufs (const std::vector<double>& values, double threshold, int& count) noexcept
{
    double energy = 0.0;
    count = 0;
    for (const auto value : values)
    {
        if (! std::isfinite (value) || value <= threshold) continue;
        energy += std::pow (10.0, value / 10.0);
        ++count;
    }
    return count > 0 ? 10.0 * std::log10 (energy / count) : std::numeric_limits<double>::quiet_NaN();
}
}

LiveWindowLoudness gatedWindowLoudness (const std::vector<double>& lufsMomentary) noexcept
{
    LiveWindowLoudness result;
    result.blocks = static_cast<int> (lufsMomentary.size());
    int absoluteCount = 0;
    const auto ungated = meanEnergyLufs (lufsMomentary, absoluteGateLufs, absoluteCount);
    if (absoluteCount == 0) return result;
    result.lufs = meanEnergyLufs (lufsMomentary, std::max (absoluteGateLufs, ungated + relativeGateLu), result.gatedBlocks);
    return result;
}

LiveWindowLoudness liveWindowLoudness (const std::vector<KirinMeterHistoryEntry>& history, int windowBlocks)
{
    std::vector<double> values;
    double maxTruePeak = -std::numeric_limits<double>::infinity();
    if (history.empty() || windowBlocks <= 0) return {};
    const auto& last = history.back();
    for (auto it = history.rbegin(); it != history.rend() && static_cast<int> (values.size()) < windowBlocks; ++it)
    {
        // 測定区間が変わったところ（engine の作り直し・測定のやり直し）より前は使わない。
        if (it->measurement_epoch != last.measurement_epoch || it->run_id != last.run_id
            || it->resolution != KIRIN_METER_HISTORY_10_HZ || it->observation_count == 0)
            break;
        values.push_back (it->lufs_m.mean);
        if (std::isfinite (it->true_peak.max)) maxTruePeak = std::max (maxTruePeak, it->true_peak.max);
    }
    std::reverse (values.begin(), values.end());
    auto result = gatedWindowLoudness (values);
    if (std::isfinite (maxTruePeak)) result.maxTruePeak = maxTruePeak;
    return result;
}
}
