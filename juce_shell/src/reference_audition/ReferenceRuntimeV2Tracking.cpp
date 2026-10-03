#include "ReferenceRuntimeV2Controller.h"

#include <cmath>
#include <limits>

namespace hypha::reference_audition
{
namespace
{
constexpr double unavailable = std::numeric_limits<double>::quiet_NaN();

double louder (double first, double second) noexcept
{
    return ! std::isfinite (first) ? second : ! std::isfinite (second) ? first : std::max (first, second);
}
}

int RuntimeV2Controller::matchWindowBlocks() const
{
    if (trackingEnabled.load (std::memory_order_acquire)) return liveWindowBlocks;
    const juce::ScopedLock lock (stateLock);
    return currentSnapshot.cueWindowBlocks;
}

// H3: 選んでいるあいだ 1 秒ごとに、A の直近の窓から gain を求め直す（メッセージスレッド）。
// B は「A の直近 10 秒 − 鳴らしている Cue の値（無ければ曲全体）」、V は「A の直近 10 秒 − 位置合わせで
// 対応する V の同じ内容」。窓が足りない（再生直後・シークの後・無音）ときは今の gain を保つ。
TrackingAction RuntimeV2Controller::followSelection (const std::vector<KirinMeterHistoryEntry>& history,
                                                     double aSessionPeakDbtp) noexcept
{
    if (! trackingAudible() || ! ready.load (std::memory_order_acquire)) return TrackingAction::keep;
    const auto generation = normalSelectionGeneration.load (std::memory_order_acquire);
    std::shared_ptr<const RuntimeSource> source;
    juce::String mode;
    double currentGain = 0.0, selectionAPeak = unavailable, cueLoudness = unavailable, cuePeak = unavailable;
    bool cueLevel = false;
    {
        const juce::ScopedLock lock (stateLock);
        if (currentSnapshot.tracking != TrackingState::following) return TrackingAction::keep;
        source = publishedSource;
        mode = currentSnapshot.comparisonMode;
        currentGain = currentSnapshot.appliedGainDb;
        selectionAPeak = currentSnapshot.aMaximumTruePeakDbtp;
        cueLevel = currentSnapshot.cueLevelAvailable && ! versionComparison;
        cueLoudness = currentSnapshot.cueIntegratedLoudness;
        cuePeak = currentSnapshot.cueMaximumTruePeakDbtp;
    }
    if (source == nullptr) return TrackingAction::keep;
    const auto& summary = source->measurementSummary;
    const auto sourceLoudness = cueLevel ? cueLoudness
        : summary && summary->loudnessLufsI ? *summary->loudnessLufsI : unavailable;
    const auto sourcePeak = cueLevel && std::isfinite (cuePeak) ? cuePeak
        : summary && summary->maximumTruePeakDbtp ? *summary->maximumTruePeakDbtp : unavailable;
    double aLoudness = unavailable, required = unavailable;
    if (versionComparison)
    {
        const auto paired = pairedWindowLoudness (history, visualBinding());
        if (paired.a.gatedBlocks < liveWindowMinimumGatedBlocks
            || paired.reference.gatedBlocks < liveWindowMinimumGatedBlocks)
            return TrackingAction::keep;
        required = paired.a.lufs - paired.reference.lufs;
    }
    else
    {
        if (mode != "loudness_match") return TrackingAction::keep;
        const auto window = liveWindowLoudness (history);
        if (window.gatedBlocks < liveWindowMinimumGatedBlocks) return TrackingAction::keep;
        aLoudness = window.lufs;
        required = aLoudness - sourceLoudness;
    }
    const auto step = trackingStep (required, currentGain, sourcePeak, louder (aSessionPeakDbtp, selectionAPeak));
    if (step.action == TrackingAction::keep) return TrackingAction::keep;

    const juce::ScopedLock lock (stateLock);
    if (normalSelectionGeneration.load (std::memory_order_acquire) != generation
        || ! bSelected.load (std::memory_order_acquire) || currentSnapshot.tracking != TrackingState::following)
        return TrackingAction::keep;
    if (step.action == TrackingAction::stopCeiling)
    {
        currentSnapshot.tracking = TrackingState::stoppedCeiling;
        holdCurrentGainLocked();
        return TrackingAction::stopCeiling;
    }
    bLinearGain.store (static_cast<float> (std::pow (10.0, step.gainDb / 20.0)), std::memory_order_release);
    auto& state = currentSnapshot;
    state.appliedGainDb = step.gainDb;
    if (! versionComparison) state.aIntegratedLoudness = aLoudness;
    state.aMaximumTruePeakDbtp = louder (aSessionPeakDbtp, selectionAPeak);
    state.adjustedBIntegratedLoudness = std::isfinite (sourceLoudness) ? sourceLoudness + step.gainDb : unavailable;
    state.adjustedBMaximumTruePeakDbtp = std::isfinite (sourcePeak) ? sourcePeak + step.gainDb : unavailable;
    state.loudnessDeltaBMinusA = std::isfinite (state.aIntegratedLoudness) && std::isfinite (state.adjustedBIntegratedLoudness)
        ? state.adjustedBIntegratedLoudness - state.aIntegratedLoudness : unavailable;
    state.truePeakDeltaBMinusA = std::isfinite (state.aMaximumTruePeakDbtp) && std::isfinite (state.adjustedBMaximumTruePeakDbtp)
        ? state.adjustedBMaximumTruePeakDbtp - state.aMaximumTruePeakDbtp : unavailable;
    holdCurrentGainLocked();
    return TrackingAction::move;
}
}
