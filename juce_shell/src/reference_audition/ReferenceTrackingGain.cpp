#include "ReferenceTrackingGain.h"
#include "ReferenceVisualTimeline.h"

#include <cmath>

namespace hypha::reference_audition
{
double referenceGainHeadroomDb (double sourcePeakDbtp, double aPeakDbtp) noexcept
{
    if (! std::isfinite (sourcePeakDbtp)) return 0.0;
    const auto ceiling = std::max ({ -1.0, std::isfinite (aPeakDbtp) ? aPeakDbtp : -1.0, sourcePeakDbtp });
    return std::max (0.0, ceiling - sourcePeakDbtp);
}

TrackingStep trackingStep (double requiredGainDb, double currentGainDb,
                           double sourcePeakDbtp, double aPeakDbtp) noexcept
{
    if (! std::isfinite (requiredGainDb) || requiredGainDb < -100.0 || requiredGainDb > 100.0
        || ! std::isfinite (currentGainDb) || std::abs (requiredGainDb - currentGainDb) < trackingToleranceDb)
        return {};
    if (requiredGainDb > 0.0 && referenceGainHeadroomDb (sourcePeakDbtp, aPeakDbtp) + 1.0e-9 < requiredGainDb)
        return { TrackingAction::stopCeiling, requiredGainDb };
    return { TrackingAction::move, requiredGainDb };
}

PairedWindowLoudness pairedWindowLoudness (const std::vector<KirinMeterHistoryEntry>& history,
                                           const VisualBinding& binding, int windowBlocks)
{
    PairedWindowLoudness result;
    if (history.empty() || windowBlocks <= 0 || ! binding.aligned || binding.source == nullptr
        || binding.overview == nullptr || ! binding.overview->loudness)
        return result;
    const auto& timeline = *binding.overview->loudness;
    const auto series = timeline.series.find ("lufs_m_millilu");
    const auto sourceRate = binding.source->audio.sampleRateHz;
    if (series == timeline.series.end() || timeline.hopSamples <= 0 || sourceRate <= 0 || binding.hostRate <= 0)
        return result;
    std::vector<double> aValues, referenceValues;
    const auto& last = history.back();
    int walked = 0;
    for (auto it = history.rbegin(); it != history.rend() && walked < windowBlocks; ++it, ++walked)
    {
        // 測定区間が変わったところより前は使わない（ReferenceLiveWindowLoudness と同じ）。
        if (it->measurement_epoch != last.measurement_epoch || it->run_id != last.run_id
            || it->resolution != KIRIN_METER_HISTORY_10_HZ || it->observation_count == 0)
            break;
        std::int64_t mapped = -1;
        if (it->last_timeline_endpoint_samples == std::numeric_limits<std::int64_t>::min()
            || ! binding.mapPosition (it->last_timeline_endpoint_samples, mapped) || mapped < 0)
            continue;
        // 試聴の位置（host のサンプルレート）→ V のサンプルレートの位置 → そこで終わるブロック。
        const auto sourceSample = static_cast<std::int64_t> (
            static_cast<long double> (mapped) * sourceRate / binding.hostRate);
        const auto index = sourceSample / timeline.hopSamples - 1;
        if (index < 0 || index >= static_cast<std::int64_t> (series->second.size())) continue;
        const auto& value = series->second[static_cast<std::size_t> (index)];
        if (! value) continue;
        aValues.push_back (it->lufs_m.mean);
        referenceValues.push_back (static_cast<double> (*value) / 1000.0);
    }
    result.pairs = static_cast<int> (aValues.size());
    result.a = gatedWindowLoudness (aValues);
    result.reference = gatedWindowLoudness (referenceValues);
    return result;
}
}
