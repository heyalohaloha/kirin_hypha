#include "ReferenceComparisonController.h"
#include <cmath>

namespace hypha::reference_audition
{
namespace
{
using Stage = PendingAuditionView::Stage;
}

void ReferenceComparisonController::clearPendingAudition()
{
    activePendingIntent.store (0, std::memory_order_release);
    const juce::ScopedLock lock (selectionLock);
    version.setQueuedContentObservationEnabled (false);
    pendingAudition = {};
}

bool ReferenceComparisonController::pendingAuditionNeedsService() const
{
    return activePendingIntent.load (std::memory_order_acquire) != 0 || resumeWanted()
        || offlineRenderSeen.load (std::memory_order_acquire);
}

int ReferenceComparisonController::pendingSlot() const
{
    const juce::ScopedLock lock (selectionLock);
    return activePendingIntent.load (std::memory_order_acquire) != 0 ? pendingAudition.view.slot : 0;
}

void ReferenceComparisonController::appendPendingAudition (
    Snapshot& state, const VisualBinding& b, const VisualBinding& c) const
{
    // snapshot() already holds selectionLock. A prepared identity need not be audible yet.
    state.pendingAudition = pendingAudition.view;
    const auto& versionState = *state.versionSelection;
    const auto chosen = versionState.presetId + "/" + versionState.checkId + "/" + versionState.candidateId;
    state.versionArmable = b.source != nullptr && state.selectedVersionId.isNotEmpty()
        && state.selectedVersionId == chosen && versionState.playbackIdentity.isNotEmpty()
        && versionState.blindPhase == BlindPhase::inactive;
    state.checkArmable = c.source != nullptr && !c.hidden && state.checkSelection->playbackIdentity.isNotEmpty();
}

bool ReferenceComparisonController::requestAudition (int slot, double loudness, double peak)
{
    if (slot != 1 && slot != 2 && slot != 3) return false;
    const auto safety = pendingSafetyEpoch.load (std::memory_order_acquire);
    const auto state = snapshot();
    // 仕様 C：C は A の直近が Cue の長さ（最長 30 秒）たまってから合わせる。足りないあいだは押した選択を
    // 待たせ、たまったら鳴らす（A のまま。待ちの上限を超えたら理由を出す）。
    const bool waitForLevel = slot == 2 && state.checkSelection != nullptr && state.checkReady
        && state.checkSelection->comparisonMode == "loudness_match" && ! std::isfinite (loudness);
    // A を下げている途中（承認・bypass の後）は鳴らし始めない（下げ終わる前に鳴ると一瞬大きく聴こえる）。待たせる。
    if (state.transportPlaying && ! waitForLevel && heldA.settled())
    {
        if (slot == 1 ? selectB (loudness, peak) : slot == 2 ? selectC (loudness, peak) : selectRef (loudness, peak))
            return true;
        return waitWhilePreparing (slot);
    }
    return queueAudition (slot, safety);
}

// 押した役を待たせる（止まっている・A の音量を待つ・A を下げている途中）。再生で、準備でき次第鳴らす。
bool ReferenceComparisonController::queueAudition (int slot, std::uint64_t safety)
{
    const auto state = snapshot();
    if (trialActive()
        || !(slot == 1 ? state.versionArmable : slot == 2 ? state.checkArmable : state.referenceArmable)) return false;
    { const juce::ScopedLock lock (gateLock); if (localBlindOwned || blindGuardOwned) return false; }
    const auto identity = (slot == 1 ? *state.versionSelection : slot == 2 ? *state.checkSelection
                                                               : *state.referenceSelection).playbackIdentity;
    if (identity.isEmpty()) return false;
    selectA();
    PendingIntent next;
    next.identity = identity; next.safetyEpoch = safety;
    next.view = { slot, Stage::play };
    { const juce::ScopedLock lock (selectionLock);
      next.intentId = ++pendingSequence; pendingAudition = std::move (next);
      version.setQueuedContentObservationEnabled (slot == 1);
      activePendingIntent.store (pendingSequence, std::memory_order_release); }
    return true;
}

// 再生中に押した役が今は切り替えられないが、準備が自動で進むとき（選択の公開待ち・音源の確認・読み込み・
// 位置合わせ・A の音量待ち）は、押した役を待たせる。選択を替えた直後と同じ切替の続き（armResume が新しい公開を
// 待って待ちを立てる）で、準備でき次第新しい MATCH で鳴らす。待ちの上限は画面（H6）が見張る。待っても変わらない
// もの（MATCH の上限超え・音源の音量が無い・Cue の外・失敗）は断って理由を出す（2026-10-03、Windows の実機の
// 通しで、V の版を替えた直後や位置合わせ中に押すと「V：準備中」と出るだけで、押したことが消えていた）。
bool ReferenceComparisonController::waitWhilePreparing (int slot)
{
    if (trialActive()) return false;
    { const juce::ScopedLock lock (gateLock); if (localBlindOwned || blindGuardOwned) return false; }
    auto& target = slotController (slot);
    const auto now = target.snapshot();
    const bool settles = now.matchFailure != MatchFailure::ceilingExceeded
        && now.matchFailure != MatchFailure::sourceLevelUnavailable && ! now.auditionOutsideCue
        && now.state != RuntimeState::rejected && now.state != RuntimeState::disconnected
        && (target.requestedGeneration() != now.selectionGeneration || now.state == RuntimeState::verifying
            || (now.state == RuntimeState::ready && ! now.auditionBuffered)
            || (now.state == RuntimeState::waiting && now.rejectionCode == "reference_alignment_waiting_for_content")
            || now.matchFailure == MatchFailure::liveLevelUnavailable);
    if (! settles) return false;
    selectA();
    { const juce::ScopedLock lock (selectionLock); switchGeneration = target.requestedGeneration(); }
    switchSlot.store (slot, std::memory_order_release);
    normalOutputSlot.store (slot, std::memory_order_release);
    return true;
}

void ReferenceComparisonController::servicePendingAudition (double loudness, double peak, bool callbackLive)
{
    if (offlineRenderSeen.exchange (false, std::memory_order_acq_rel)) forgetHeldAudition();
    if (activePendingIntent.load (std::memory_order_acquire) == 0 && !armResume()) return;
    PendingIntent intent;
    { const juce::ScopedLock lock (selectionLock); intent = pendingAudition; }
    if (!intent.view.waiting() || activePendingIntent.load (std::memory_order_acquire) != intent.intentId) return;
    const auto publish = [&] (Stage stage, bool sawPlayback = false)
    {
        const juce::ScopedLock lock (selectionLock);
        if (pendingAudition.intentId == intent.intentId)
        {
            pendingAudition.view.stage = stage; pendingAudition.sawPlayback |= sawPlayback;
            if (!pendingAudition.view.waiting())
            {
                auto expected = intent.intentId; activePendingIntent.compare_exchange_strong (expected, 0);
                version.setQueuedContentObservationEnabled (false);
            }
        }
    };
    auto& target = slotController (intent.view.slot);
    const auto state = target.snapshot();
    const auto inputSafety = pendingInputSafety.load (std::memory_order_acquire);
    if (intent.safetyEpoch != pendingSafetyEpoch.load (std::memory_order_acquire)
        || (intent.sawPlayback && (!state.transportPlaying || !callbackLive))
        || (state.transportPlaying && inputSafety == 0)
        || trialActive())
    {
        // H5: 戻す選択と選択の替えは、停止・bypass のあいだ待つだけで、取り消さない（鳴らしはしない）。
        // オフライン書き出しは forgetHeldAudition で忘れる（仕様 A）。
        if ((intent.resume || intent.switching) && !trialActive())
        {
            const juce::ScopedLock lock (selectionLock);
            if (pendingAudition.intentId == intent.intentId)
            {
                pendingAudition.safetyEpoch = pendingSafetyEpoch.load (std::memory_order_acquire);
                pendingAudition.sawPlayback = false;
                pendingAudition.view.stage = Stage::play;
            }
            return;
        }
        publish (Stage::safetyChanged); return;
    }
    const auto binding = target.visualBinding();
    if (state.state == RuntimeState::rejected || state.state == RuntimeState::disconnected
        || (state.playbackIdentity.isNotEmpty() && state.playbackIdentity != intent.identity))
    { if (intent.resume || intent.switching) dropResume(); publish (Stage::sourceChanged); return; }
    if (state.sampleRateApprovalRequired) { publish (Stage::approval, state.transportPlaying); return; }
    if (!state.transportPlaying) { publish (Stage::play); return; }
    if (!callbackLive || inputSafety != 1
        || !state.transportPositionValid || state.state != RuntimeState::ready
        || !state.auditionBuffered || !binding.source)
    { publish (Stage::checking, callbackLive); return; }
    if (!intent.resume && intent.view.slot != 1 && ((state.comparisonMode == "loudness_match" && !std::isfinite (loudness))
        || (state.comparisonMode == "peak_match" && !std::isfinite (peak))))
    { publish (Stage::level, true); return; }
    if (! heldA.settled()) { publish (Stage::checking, true); return; }  // A を下げ終わってから鳴らす
    RuntimeV2SourceRepository verifier (juce::File {});
    if (verifier.verifySourceRevision (*binding.source).isNotEmpty())
    { if (intent.resume || intent.switching) dropResume(); publish (Stage::sourceChanged); return; }
    // Claim once on the control thread, then use the runtime generation across gain preparation.
    // A, a source change or reconfiguration invalidates that generation before publication.
    // Bind preparation to the same condition even if a publication changes after this snapshot.
    // Manual and queued selection share the same no-fallback MATCH policy.
    const auto generation = target.normalSelectionTicket();
    auto expected = intent.intentId;
    if (!activePendingIntent.compare_exchange_strong (expected, 0, std::memory_order_acq_rel)) return;
    if (intent.resume)
    {
        // 戻せなかった（ページの読み込み待ちなど）ときは、次の周期にもう一度戻そうとする。
        if (!target.resumeHeld (generation, intent.identity)) { publish (Stage::checking, true); return; }
        normalOutputSlot.store (intent.view.slot, std::memory_order_release);
        publish (Stage::none, true);
        return;
    }
    if (!target.selectB (loudness, peak, generation, intent.identity))
    {
        const auto failed = target.snapshot();
        // 承認は「A を差だけ下げて合わせる」。承認の後に A が大きくなって、鳴らす時点でまだ上限を超えるなら、その時点の
        // 差まで下げ直して合わせ直す（下げる向きだけ、2 回まで。2026-10-04、再生を始めた直後の見積もりが小さかった）。
        if (intent.approvedLowerA && intent.lowerRetries < 2 && failed.matchFailure == MatchFailure::ceilingExceeded
            && failed.neededAttenuationDb < heldA.targetDb() - 0.05)
        {
            heldA.hold (failed.neededAttenuationDb);
            for (auto* role : { &version, &check, &reference }) role->setHeldAttenuation (heldA.targetDb());
            const juce::ScopedLock lock (selectionLock);
            if (pendingAudition.intentId == intent.intentId)
            {
                ++pendingAudition.lowerRetries;
                pendingAudition.view.stage = Stage::checking;
                activePendingIntent.store (intent.intentId, std::memory_order_release);
            }
            return;
        }
        if (intent.switching) dropResume();  // 新しい MATCH ができない：選択を手放して理由を出す（R-28）
        const auto failure = failed.matchFailure;
        publish (failure == MatchFailure::ceilingExceeded ? Stage::ceilingExceeded
            : failure == MatchFailure::sourceLevelUnavailable ? Stage::sourceLevelUnavailable
            : Stage::startFailed, true);
        return;
    }
    normalOutputSlot.store (intent.view.slot, std::memory_order_release);
    publish (Stage::none, true);
}
}
