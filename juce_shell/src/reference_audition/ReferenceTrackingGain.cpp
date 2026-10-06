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

bool referenceGainExceedsCeiling (double requiredGainDb, double sourcePeakDbtp, double aPeakDbtp,
                                  double heldAttenuationDb) noexcept
{
    const auto held = std::isfinite (heldAttenuationDb) ? std::min (0.0, heldAttenuationDb) : 0.0;
    const auto effective = requiredGainDb + held;
    if (! (effective > 0.0)) return false;
    return referenceGainHeadroomDb (sourcePeakDbtp, std::isfinite (aPeakDbtp) ? aPeakDbtp + held : aPeakDbtp)
        + 1.0e-9 < effective;
}

double referencePeakShortfallDb (double requiredGainDb, double sourcePeakDbtp, double aPeakDbtp,
                                 double heldAttenuationDb) noexcept
{
    if (! referenceGainExceedsCeiling (requiredGainDb, sourcePeakDbtp, aPeakDbtp, heldAttenuationDb)) return 0.0;
    const auto held = std::isfinite (heldAttenuationDb) ? std::min (0.0, heldAttenuationDb) : 0.0;
    return requiredGainDb + held
        - referenceGainHeadroomDb (sourcePeakDbtp, std::isfinite (aPeakDbtp) ? aPeakDbtp + held : aPeakDbtp);
}

double referenceAttenuationToMatch (double requiredGainDb) noexcept
{
    return std::isfinite (requiredGainDb) && requiredGainDb > 0.0 ? -requiredGainDb : 0.0;
}

TrackingStep trackingStep (double requiredGainDb, double currentGainDb, double sourcePeakDbtp, double aPeakDbtp,
                           double anchorGainDb, double heldAttenuationDb) noexcept
{
    if (! std::isfinite (requiredGainDb) || requiredGainDb < -100.0 || requiredGainDb > 100.0
        || ! std::isfinite (currentGainDb) || std::abs (requiredGainDb - currentGainDb) < trackingToleranceDb)
        return {};
    // 上限で届かない量が 0.5 dB 以下なら上限まで上げて追従を続ける（止めない）。
    const auto shortfall = referencePeakShortfallDb (requiredGainDb, sourcePeakDbtp, aPeakDbtp, heldAttenuationDb);
    if (shortfall > peakShortfallToleranceDb + 1.0e-9)
        return { TrackingAction::stopCeiling, requiredGainDb };
    const auto target = requiredGainDb - shortfall;
    if (std::isfinite (anchorGainDb) && std::abs (target - anchorGainDb) > trackingRangeDb + 1.0e-9)
        return { TrackingAction::stopRange, target };
    return { TrackingAction::move, target, shortfall };
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
