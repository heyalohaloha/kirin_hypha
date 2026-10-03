#include "ReferenceRuntimeV2Controller.h"

namespace hypha::reference_audition
{
// H5: 停止・シーク・ページの読み込み待ちで A に戻った選択を、準備でき次第そのまま戻す（live PRE/POST 比較と
// 同じ「選択を保ち、確かめられたら戻る」）。戻すのは同じ音（playback identity）だけで、gain は止まる前に
// 掛けていた値（追従で動いた後の値）。MATCH は測り直さない（C の固定を崩さない・窓が空でも gain が飛ばない）。
bool RuntimeV2Controller::resumeHeld (std::uint64_t selectionGeneration, const juce::String& playbackIdentity) noexcept
{
    if (blind.ongoing() || bSelected.load (std::memory_order_acquire) || revokeAfterFade.load (std::memory_order_acquire))
        return false;
    {
        const juce::ScopedLock lock (stateLock);
        if (! heldSelection.valid || playbackIdentity.isEmpty() || heldSelection.playbackIdentity != playbackIdentity
            || currentSnapshot.playbackIdentity != playbackIdentity || ! ready.load (std::memory_order_acquire)
            || normalSelectionGeneration.load (std::memory_order_acquire) != selectionGeneration)
            return false;
        auto prepared = heldSelection.facts;
        prepared.auditionEpoch = auditionEpoch.load (std::memory_order_acquire);
        prepared.selectionGeneration = selectionGeneration;
        prepared.valid = true;
        preparedNormalSelection = prepared;
    }
    const auto bBaseline = bAudibleConfirmations.load (std::memory_order_acquire);
    if (! activatePreparedB (selectionGeneration)) return false;
    beginAuditionEventSession (bBaseline);
    return true;
}

std::uint64_t RuntimeV2Controller::requestedGeneration() const
{
    const juce::ScopedLock lock (stateLock);
    return requestedSelection.generation;
}

bool RuntimeV2Controller::hasHeldSelection() const
{
    const juce::ScopedLock lock (stateLock);
    return heldSelection.valid;
}

juce::String RuntimeV2Controller::heldPlaybackIdentity() const
{
    const juce::ScopedLock lock (stateLock);
    return heldSelection.valid ? heldSelection.playbackIdentity : juce::String {};
}

void RuntimeV2Controller::forgetHeldSelection()
{
    const juce::ScopedLock lock (stateLock);
    heldSelection = {};
}

void RuntimeV2Controller::holdCurrentGainLocked() noexcept
{
    if (! heldSelection.valid) return;
    auto& facts = heldSelection.facts;
    facts.linearGain = bLinearGain.load (std::memory_order_acquire);
    facts.appliedGainDb = currentSnapshot.appliedGainDb;
    facts.aIntegratedLoudness = currentSnapshot.aIntegratedLoudness;
    facts.aMaximumTruePeakDbtp = currentSnapshot.aMaximumTruePeakDbtp;
    facts.adjustedBIntegratedLoudness = currentSnapshot.adjustedBIntegratedLoudness;
    facts.adjustedBMaximumTruePeakDbtp = currentSnapshot.adjustedBMaximumTruePeakDbtp;
    facts.loudnessDeltaBMinusA = currentSnapshot.loudnessDeltaBMinusA;
    facts.truePeakDeltaBMinusA = currentSnapshot.truePeakDeltaBMinusA;
    facts.tracking = currentSnapshot.tracking;
}
}
