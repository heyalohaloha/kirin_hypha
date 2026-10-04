#include "ReferenceDynamicsRange.h"

#include <algorithm>
#include <cmath>

namespace hypha::reference_audition
{
namespace
{
constexpr double nan = std::numeric_limits<double>::quiet_NaN();
constexpr double silenceDb = -299.999;  // Kirin OS の millidb(0) は −300000（無音）

double decibels (double amplitude) noexcept { return amplitude > 0.0 ? 20.0 * std::log10 (amplitude) : nan; }

// 区間 [start, end) に丸ごと入る、長さ hop の区間の番号。短い Cue（1 区間に満たない）は重なる区間を使う。
std::pair<std::size_t, std::size_t> hopIndices (std::int64_t hop, std::int64_t start, std::int64_t end, std::size_t size) noexcept
{
    if (hop < 1 || end <= start || start < 0) return { 0, 0 };
    auto first = (start + hop - 1) / hop, last = end / hop;
    if (last <= first) { first = start / hop; last = (end + hop - 1) / hop; }
    const auto clamp = [size] (std::int64_t value) { return static_cast<std::size_t> (std::clamp<std::int64_t> (value, 0, static_cast<std::int64_t> (size))); };
    return { clamp (first), clamp (last) };
}

std::vector<double> scaled (const RuntimeNullableIntegerSeries* series, std::int64_t hop, std::int64_t start, std::int64_t end,
                            double scale, std::size_t count)
{
    std::vector<double> values (count, nan);
    if (series == nullptr) return values;
    const auto [first, last] = hopIndices (hop, start, end, series->size());
    for (std::size_t index = first; index < last && index - first < count; ++index)
        if ((*series)[index]) values[index - first] = static_cast<double> (*(*series)[index]) / scale;
    return values;
}

const RuntimeNullableIntegerSeries* find (const std::optional<RuntimeMeasurementTimeline>& timeline, const char* name)
{
    if (! timeline) return nullptr;
    const auto found = timeline->series.find (name);
    return found == timeline->series.end() ? nullptr : &found->second;
}

// アタック：同じ区間のピーク − LUFS-M（どちらか無ければ無い）。
double attackOf (double peakDb, double momentaryLufs) noexcept
{
    return std::isfinite (peakDb) && std::isfinite (momentaryLufs) ? peakDb - momentaryLufs : nan;
}
}

DynamicsHops aggregateHops (const KirinReferenceVisualBin* bins, std::size_t count, int binsPerHop, int channels)
{
    DynamicsHops hops;
    if (bins == nullptr || binsPerHop < 1 || channels < 1 || channels > 2) return hops;
    const auto hopCount = count / static_cast<std::size_t> (binsPerHop);
    const auto first = count - hopCount * static_cast<std::size_t> (binsPerHop);  // 古い側の足りない端を捨てる
    for (auto& values : hops.values) values.reserve (hopCount);
    double previousRms = nan;  // 窓の前の区間は知らない（最初の立ち上がりは出さない）
    for (std::size_t hop = 0; hop < hopCount; ++hop)
    {
        const auto* begin = bins + first + hop * static_cast<std::size_t> (binsPerHop);
        double frames = 0.0, tp = 0.0, cross = 0.0, mid = 0.0, side = 0.0;
        std::array<double, 2> energy {}, peak {};
        for (int index = 0; index < binsPerHop; ++index)
        {
            const auto& bin = begin[index];
            const auto binFrames = static_cast<double> (bin.frames);
            frames += binFrames;
            for (int c = 0; c < channels; ++c)
            {
                energy[static_cast<std::size_t> (c)] += bin.rms[c] * bin.rms[c] * binFrames;
                peak[static_cast<std::size_t> (c)] = std::max (peak[static_cast<std::size_t> (c)], bin.peak[c]);
            }
            tp = std::max (tp, bin.true_peak);
            cross += bin.cross;
            mid += bin.mid;
            side += bin.side;
        }
        hops.hopSamples = std::max<std::int64_t> (hops.hopSamples, static_cast<std::int64_t> (frames));
        const auto& last = begin[binsPerHop - 1];
        const auto rms = frames > 0.0 ? std::sqrt ((energy[0] + energy[1]) / (frames * channels)) : 0.0;
        const bool stereo = channels == 2;
        hops[DynamicsFact::crest].push_back (rms > 1e-15 && tp > 0.0 ? 20.0 * std::log10 (tp / rms) : nan);
        hops[DynamicsFact::lufsM].push_back (std::isfinite (last.momentary_lufs) ? last.momentary_lufs : nan);
        hops[DynamicsFact::lufsS].push_back (std::isfinite (last.short_lufs) ? last.short_lufs : nan);
        hops[DynamicsFact::correlation].push_back (stereo && energy[0] > 0.0 && energy[1] > 0.0
            ? std::clamp (cross / std::sqrt (energy[0] * energy[1]), -1.0, 1.0) : nan);
        hops[DynamicsFact::width].push_back (stereo && mid + side > 0.0
            ? std::min (150.0, std::sqrt (side / frames) / (std::sqrt (mid / frames) + 1e-10) * 100.0) : nan);
        hops[DynamicsFact::peak].push_back (decibels (std::max (peak[0], peak[1])));
        hops[DynamicsFact::rms].push_back (decibels (rms));
        hops[DynamicsFact::onset].push_back (rms > 1e-15 ? (std::isfinite (previousRms) ? std::max (0.0, rms - previousRms) / rms : nan) : 0.0);
        hops[DynamicsFact::attack].push_back (attackOf (hops[DynamicsFact::peak].back(), hops[DynamicsFact::lufsM].back()));
        previousRms = rms;
    }
    return hops;
}

DynamicsHops kirinHops (const RuntimeDetailedMeasurement& measurement, std::int64_t startSample, std::int64_t endSample)
{
    DynamicsHops hops;
    const auto hop = measurement.dynamics ? measurement.dynamics->hopSamples
        : measurement.loudness ? measurement.loudness->hopSamples : measurement.stereo ? measurement.stereo->hopSamples : 0;
    const auto* crest = find (measurement.dynamics, "crest_millidb");
    std::size_t count = 0;
    for (const auto* series : { crest, find (measurement.loudness, "lufs_m_millilu"), find (measurement.stereo, "correlation_milli") })
        if (series != nullptr)
        {
            const auto [first, last] = hopIndices (hop, startSample, endSample, series->size());
            count = std::max (count, last - first);
        }
    hops.hopSamples = hop;
    const auto timelineHop = [hop] (const std::optional<RuntimeMeasurementTimeline>& timeline)
    { return timeline ? timeline->hopSamples : hop; };
    hops[DynamicsFact::crest] = scaled (crest, timelineHop (measurement.dynamics), startSample, endSample, 1000.0, count);
    hops[DynamicsFact::lufsM] = scaled (find (measurement.loudness, "lufs_m_millilu"), timelineHop (measurement.loudness), startSample, endSample, 1000.0, count);
    hops[DynamicsFact::lufsS] = scaled (find (measurement.loudness, "lufs_s_millilu"), timelineHop (measurement.loudness), startSample, endSample, 1000.0, count);
    hops[DynamicsFact::width] = scaled (find (measurement.stereo, "width_basis_points"), timelineHop (measurement.stereo), startSample, endSample, 100.0, count);
    hops[DynamicsFact::correlation] = scaled (find (measurement.stereo, "correlation_milli"), timelineHop (measurement.stereo), startSample, endSample, 1000.0, count);
    hops[DynamicsFact::peak].assign (count, nan);
    hops[DynamicsFact::rms].assign (count, nan);
    if (measurement.waveform && ! measurement.waveform->samplePeakMillidbfs.empty())
    {
        const auto& waveform = *measurement.waveform;
        const auto [first, last] = hopIndices (waveform.framesPerBin, startSample, endSample, waveform.samplePeakMillidbfs.front().size());
        for (std::size_t index = first; index < last && index - first < count; ++index)
        {
            double peak = -std::numeric_limits<double>::infinity(), power = 0.0;
            int channels = 0;
            for (std::size_t c = 0; c < waveform.samplePeakMillidbfs.size(); ++c)
            {
                if (index >= waveform.samplePeakMillidbfs[c].size() || c >= waveform.rmsMillidbfs.size()
                    || index >= waveform.rmsMillidbfs[c].size()) continue;
                peak = std::max (peak, static_cast<double> (waveform.samplePeakMillidbfs[c][index]) / 1000.0);
                power += std::pow (10.0, static_cast<double> (waveform.rmsMillidbfs[c][index]) / 10'000.0);
                ++channels;
            }
            if (channels == 0) continue;
            hops[DynamicsFact::peak][index - first] = peak > silenceDb ? peak : nan;
            const auto rms = 10.0 * std::log10 (power / channels);
            hops[DynamicsFact::rms][index - first] = rms > silenceDb ? rms : nan;
        }
    }
    hops[DynamicsFact::onset].assign (count, nan);
    if (measurement.transient)
    {
        const auto& onset = measurement.transient->onsetStrengthQ15;
        const auto [first, last] = hopIndices (measurement.transient->hopSamples, startSample, endSample, onset.size());
        for (std::size_t index = first; index < last && index - first < count; ++index)
            hops[DynamicsFact::onset][index - first] = static_cast<double> (onset[index]) / 32'767.0;
    }
    hops[DynamicsFact::attack].assign (count, nan);
    for (std::size_t index = 0; index < count; ++index)
        if (index < hops[DynamicsFact::peak].size() && index < hops[DynamicsFact::lufsM].size())
            hops[DynamicsFact::attack][index] = attackOf (hops[DynamicsFact::peak][index], hops[DynamicsFact::lufsM][index]);
    return hops;
}

FactRange rangeOf (const std::vector<double>& values)
{
    std::vector<double> finite;
    finite.reserve (values.size());
    for (const auto value : values) if (std::isfinite (value)) finite.push_back (value);
    FactRange range;
    if (finite.empty()) return range;
    std::sort (finite.begin(), finite.end());
    const auto at = [&finite] (double ratio)
    { return finite[static_cast<std::size_t> (std::llround (static_cast<double> (finite.size() - 1) * ratio))]; };
    range.p10 = at (0.1);
    range.median = at (0.5);
    range.p90 = at (0.9);
    range.count = static_cast<int> (finite.size());
    return range;
}
}
