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
    double currentGain = 0.0, anchorGain = unavailable, selectionAPeak = unavailable, cueLoudness = unavailable, cuePeak = unavailable;
    bool cueLevel = false;
    {
        const juce::ScopedLock lock (stateLock);
        if (currentSnapshot.tracking != TrackingState::following) return TrackingAction::keep;
        source = publishedSource;
        mode = currentSnapshot.comparisonMode;
        currentGain = currentSnapshot.appliedGainDb;
        anchorGain = trackingAnchorDb;
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
    const auto step = trackingStep (required, currentGain, sourcePeak, louder (aSessionPeakDbtp, selectionAPeak), anchorGain,
                                    heldAttenuationDb.load (std::memory_order_acquire));
    if (step.action == TrackingAction::keep) return TrackingAction::keep;

    const juce::ScopedLock lock (stateLock);
    if (normalSelectionGeneration.load (std::memory_order_acquire) != generation
        || ! bSelected.load (std::memory_order_acquire) || currentSnapshot.tracking != TrackingState::following)
        return TrackingAction::keep;
    if (step.action == TrackingAction::stopCeiling || step.action == TrackingAction::stopRange)
    {
        currentSnapshot.tracking = step.action == TrackingAction::stopCeiling ? TrackingState::stoppedCeiling
                                                                             : TrackingState::stoppedRange;
        holdCurrentGainLocked();
        return step.action;
    }
    applyMatchedGainLocked (step.gainDb, versionComparison ? currentSnapshot.aIntegratedLoudness : aLoudness,
                            louder (aSessionPeakDbtp, selectionAPeak), sourceLoudness, sourcePeak);
    return TrackingAction::move;
}

void RuntimeV2Controller::applyMatchedGainLocked (double gainDb, double aLoudness, double aPeakDbtp,
                                                  double sourceLoudness, double sourcePeakDbtp) noexcept
{
    bLinearGain.store (static_cast<float> (std::pow (10.0, gainDb / 20.0)), std::memory_order_release);
    auto& state = currentSnapshot;
    state.appliedGainDb = gainDb;
    state.aIntegratedLoudness = aLoudness;
    state.aMaximumTruePeakDbtp = aPeakDbtp;
    state.adjustedBIntegratedLoudness = std::isfinite (sourceLoudness) ? sourceLoudness + gainDb : unavailable;
    state.adjustedBMaximumTruePeakDbtp = std::isfinite (sourcePeakDbtp) ? sourcePeakDbtp + gainDb : unavailable;
    state.loudnessDeltaBMinusA = std::isfinite (state.aIntegratedLoudness) && std::isfinite (state.adjustedBIntegratedLoudness)
        ? state.adjustedBIntegratedLoudness - state.aIntegratedLoudness : unavailable;
    state.truePeakDeltaBMinusA = std::isfinite (state.aMaximumTruePeakDbtp) && std::isfinite (state.adjustedBMaximumTruePeakDbtp)
        ? state.adjustedBMaximumTruePeakDbtp - state.aMaximumTruePeakDbtp : unavailable;
    holdCurrentGainLocked();
}

// H7: V の自動特定。Version の指紋は作業スレッドが ranges から読んでおき（library が変わったとき・読めなかった
// ものを読み直すとき）、ここでは A の直近と照合するだけ（メッセージスレッドでファイルを読まない）。
VersionIdentity RuntimeV2Controller::identifyVersions (const KirinFingerprint& slice, std::int64_t endTick)
{
    if (! versionComparison) return {};
    return versionIdentifier.identify (slice, endTick);
}

// H12: C の MATCH をもう一度（方向設計 §4 の C の画面、右上の MATCH）。鳴っている C の gain を
// 「A の直近（Cue と同じ長さ）− Cue の Kirin OS の値（無ければ曲全体）」に決め直して固定する。追従は
// しない（H4）。上限（True Peak）を超えるなら今の gain を保って理由を返す（R-28）。A は動かさない。
RematchResult RuntimeV2Controller::rematch (double aLoudness, double aSessionPeakDbtp) noexcept
{
    if (versionComparison || trackingEnabled.load (std::memory_order_acquire)
        || ! ready.load (std::memory_order_acquire) || ! bSelected.load (std::memory_order_acquire))
        return RematchResult::notPlaying;
    const auto generation = normalSelectionGeneration.load (std::memory_order_acquire);
    std::shared_ptr<const RuntimeSource> source;
    double selectionAPeak = unavailable, cueLoudness = unavailable, cuePeak = unavailable;
    bool cueLevel = false;
    {
        const juce::ScopedLock lock (stateLock);
        if (currentSnapshot.comparisonMode == "peak_match") return RematchResult::peakMatch;
        if (currentSnapshot.tracking == TrackingState::none || currentSnapshot.comparisonMode != "loudness_match")
            return RematchResult::original;
        source = publishedSource;
        selectionAPeak = currentSnapshot.aMaximumTruePeakDbtp;
        cueLevel = currentSnapshot.cueLevelAvailable;
        cueLoudness = currentSnapshot.cueIntegratedLoudness;
        cuePeak = currentSnapshot.cueMaximumTruePeakDbtp;
    }
    if (source == nullptr) return RematchResult::notPlaying;
    const auto& summary = source->measurementSummary;
    const auto sourceLoudness = cueLevel ? cueLoudness
        : summary && summary->loudnessLufsI ? *summary->loudnessLufsI : unavailable;
    const auto sourcePeak = cueLevel && std::isfinite (cuePeak) ? cuePeak
        : summary && summary->maximumTruePeakDbtp ? *summary->maximumTruePeakDbtp : unavailable;
    const auto required = aLoudness - sourceLoudness;
    if (! std::isfinite (required) || required < -100.0 || required > 100.0) return RematchResult::levelUnavailable;
    const auto aPeak = louder (aSessionPeakDbtp, selectionAPeak);
    if (referenceGainExceedsCeiling (required, sourcePeak, aPeak, heldAttenuationDb.load (std::memory_order_acquire)))
    {
        const juce::ScopedLock lock (stateLock);
        currentSnapshot.matchFailure = MatchFailure::ceilingExceeded;
        currentSnapshot.neededAttenuationDb = referenceAttenuationToMatch (required);  // 承認すれば合わせられる下げ幅
        return RematchResult::ceilingExceeded;
    }

    const juce::ScopedLock lock (stateLock);
    if (normalSelectionGeneration.load (std::memory_order_acquire) != generation || ! bSelected.load (std::memory_order_acquire))
        return RematchResult::notPlaying;
    currentSnapshot.tracking = TrackingState::fixed;
    applyMatchedGainLocked (required, aLoudness, aPeak, sourceLoudness, sourcePeak);
    trackingAnchorDb = required;
    if (heldSelection.valid) heldSelection.facts.anchorGainDb = required;
    return RematchResult::matched;
}
}
