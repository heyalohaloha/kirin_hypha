#include "ReferenceComparisonController.h"
#include <algorithm>

namespace hypha::reference_audition
{
ReferenceComparisonController::ReferenceComparisonController (juce::File root, SelectionGate callback,
    SelectionGate blindCallback, StateChanged stateChangedIn, ExternalStates externalIn)
    : gate (std::move (callback)), versionBlindGate (std::move (blindCallback)), externalStates (std::move (externalIn)),
      stateChanged(std::move(stateChangedIn)),
      version (root, [this] (bool active) { return admit (1, active); }, true),
      check (root, [this] (bool active) { return admit (2, active); }, false),
      reference (root, [this] (bool active) { return admit (3, active); }, false),
      visual ([this] {
          return slotController (viewedSlot.load (std::memory_order_acquire)).visualBinding();
      },analysis)
{
    reference.setTrackingEnabled (true);  // B は A の直近 10 秒に追従する
    reference.setSongsOnly (true);        // B は B セットの曲だけを鳴らす（C の Preset に落ちない）
}

ReferenceComparisonController::~ReferenceComparisonController()
{
    setPresented (false);
    aFeed.store (false);
    while (aWriters.load() != 0) juce::Thread::yield();  // Audio Thread が A を渡し終えるまで
    suspendAudition();
    const juce::ScopedLock lock (gateLock); closing = true; blindSlot.close();
    visual.pauseAdmission(); if (gateOwners && gate) gate (false); gateOwners = 0;
    if(blindGuardOwned && versionBlindGate) versionBlindGate(false); blindGuardOwned=false;
}

void ReferenceComparisonController::setPresented (bool active) noexcept
{ presented=active; refreshObservation(); }

bool ReferenceComparisonController::admit (int slot, bool active)
{
    const auto outside = active ? external() : 0;
    const juce::ScopedLock lock (gateLock);
    if (closing) return !active;
    const int bit = 1 << slot;
    if (active)
    {
        if ((gateOwners & bit) != 0) return false;
        // B・C・V が出力を取る最後の段：表で調べてから押さえる（押した・待たせた・戻す・替えた選択のどれも、ここを通る）。
        // VERSION BLIND の V は、始めるときに表を通っている。
        if (! (slot == 1 && blindGuardOwned)
            && output_owner::decide (output_owner::Activity::audition, outside | ownStatesLocked()).refused())
            return false;
        if ((gateOwners & 2) != 0 && !version.canTransferOutputGate()) return false;
        if ((gateOwners & 4) != 0 && !check.canTransferOutputGate()) return false;
        if ((gateOwners & 8) != 0 && !reference.canTransferOutputGate()) return false;
        if (gateOwners == 0)
        {
            if(gate && !gate(true)) return false;
        }
        gateOwners |= bit;
    }
    else if ((gateOwners & bit) != 0)
    {
        gateOwners &= ~bit;
        // VERSION BLIND が終わった後に V が A へ戻し終えたら、ほかの Blind との排他を解く。始めるときに V の通常の試聴が
        // 出力を返すのは Blind の途中（aInputPaused）なので、解かない。
        if (slot == 1 && blindGuardOwned && ! aInputPaused) releaseVersionBlindGuard();
        if (gateOwners == 0)
        {
            if (gate) gate (false);
        }
    }
    return true;
}

output_owner::States ReferenceComparisonController::ownStatesLocked() const
{
    using namespace output_owner;
    States states = 0;
    if (blindGuardOwned || aInputPaused || blindSessionOpen.load (std::memory_order_acquire)) states |= bit (State::versionBlind);
    if (localBlindOwned) states |= bit (State::localBlind);
    if (heldA.returning()) states |= bit (State::referenceReturning);
    else if (heldA.held()) states |= bit (State::referenceLowered);
    if ((gateOwners & ~(blindGuardOwned ? 2 : 0)) != 0) states |= bit (State::audition);
    if (normalOutputSlot.load (std::memory_order_acquire) != 0 || activePendingIntent.load (std::memory_order_acquire) != 0)
        states |= bit (State::auditionHeld);
    return states;
}

output_owner::States ReferenceComparisonController::ownOutputStates() const
{
    const juce::ScopedLock lock (gateLock);
    return ownStatesLocked();
}

output_owner::Decision ReferenceComparisonController::outputDecision (output_owner::Activity activity) const
{
    const auto outside = external();
    const juce::ScopedLock lock (gateLock);
    return output_owner::decide (activity, outside | ownStatesLocked());
}

void ReferenceComparisonController::configure (RuntimeIdentity identity, double rate, int channels)
{
    clearPendingAudition();
    {
        const juce::ScopedLock lock (selectionLock);
        if (receiverId != identity.runtimeInstanceId && ! pendingSettings) versionId.clear();
        receiverId = identity.runtimeInstanceId;
        versionChosen.store (versionId.isNotEmpty(), std::memory_order_release);
    }
    visual.configure(rate,channels);
    heldA.prepare (rate);
    auto bIdentity = identity;
    bIdentity.runtimeInstanceId += ".version";
    version.configure (bIdentity, rate, channels);
    check.configure (identity, rate, channels);
    auto rIdentity = identity;
    rIdentity.runtimeInstanceId += ".reference";
    reference.configure (rIdentity, rate, channels);
    std::optional<ReferenceComparisonSettings> pending;
    { const juce::ScopedLock lock (selectionLock); configured = true; pending = pendingSettings; }
    if (pending) restoreSettings (*pending);
    rtPlaying = false;
    rtInputAllowed = false;
    rtInputObserved = false;
    pendingInputSafety.store (-1, std::memory_order_release);
}

ReferenceComparisonSettings ReferenceComparisonController::savedSettings()
{
    const auto b = version.snapshot();
    const juce::ScopedLock lock (selectionLock);
    if (pendingSettings) return *pendingSettings;
    ReferenceComparisonSettings result;
    result.version = versionId.isEmpty() ? ReferenceChoice {} : version.savedChoice();
    result.versionAuto = versionAuto && versionId.isNotEmpty();
    // A removed Version must not be replaced by a fallback selection on save.
    if (versionId.isNotEmpty() && b.migratedVersionChoice != versionId)
    {
        const auto ids = juce::StringArray::fromTokens (versionId, "/", {});
        if (ids.size() == 3)
        { result.version.presetId = ids[0]; result.version.checkId = ids[1]; result.version.candidateId = ids[2]; }
    }
    result.check = check.savedChoice();
    result.reference = songId.isEmpty() ? ReferenceChoice {} : reference.savedChoice();
    result.reference.cueId.clear();  // B の曲は既定の Cue で鳴らす
    result.songSetId = songSetId;
    result.visualView = visualPreferences->get();
    result.viewedSlot = viewedSlot.load (std::memory_order_acquire);
    return result;
}

void ReferenceComparisonController::restoreSettings (const ReferenceComparisonSettings& input)
{
    ReferenceComparisonSettings value = input;
    visualPreferences->set (value.visualView);
    if (! value.version.valid() || value.version.candidateId.isEmpty()) value.version = {};
    if (! value.check.valid()) value.check = {};
    if (! value.reference.valid() || value.reference.candidateId.isEmpty()) value.reference = {};
    selectA();
    bool apply = false;
    {
        const juce::ScopedLock lock (selectionLock);
        versionId = value.version.candidateId.isEmpty() ? juce::String {} : value.version.target();
        versionAuto = value.versionAuto && versionId.isNotEmpty();
        versionChosen.store (versionId.isNotEmpty(), std::memory_order_release);
        viewedSlot.store (value.viewedSlot == 1 || value.viewedSlot == 3 ? value.viewedSlot : 2, std::memory_order_release);
        songId = value.reference.candidateId.isEmpty() ? juce::String {} : value.reference.target();
        songSetId = value.songSetId;
        apply = configured;
        pendingSettings = apply ? std::optional<ReferenceComparisonSettings> {} : value;
    }
    // 2026-10-04：A の取り込みはやめた（見比べは生の表示だけ）。古い版が DAW の曲に保存した取り込みと
    // Tonal の表示の状態は読まない（ReferenceComparisonSettings が読み飛ばす）。次の保存で消える。
    if (apply) { version.restoreChoice (value.version); check.restoreChoice (value.check); reference.restoreChoice (value.reference); }
}

bool ReferenceComparisonController::trialActive() const
{
    return version.snapshot().blindPhase != BlindPhase::inactive;
}

RuntimeV2Controller& ReferenceComparisonController::viewed() noexcept
{
    return slotController (viewedSlot.load (std::memory_order_acquire));
}

Snapshot ReferenceComparisonController::snapshot()
{
    const auto b = version.snapshot(), c = check.snapshot(), r = reference.snapshot();
    ensureReferenceSong (r);
    const juce::ScopedLock lock (selectionLock);
    const auto slot = viewedSlot.load (std::memory_order_acquire);
    auto result = slot == 1 ? b : slot == 3 ? r : c;
    result.heldAttenuationDb = heldA.targetDb();
    result.lowerAOfferShownSlot = offerShownSlot;
    result.lowerAOfferShownSerial = offerShownSerial;
    result.visualTimeline = visual.snapshot();
    result.visualPreferences = visualPreferences;
    const auto viewedMap = slotController (slot).visualBinding();
    const auto versionMap = version.visualBinding();
    std::int64_t visualPosition = 0;
    if (viewedMap.aligned && !viewedMap.hidden && viewedMap.hostPositionValid && viewedMap.hostRate > 0
        && viewedMap.mapPosition (viewedMap.hostPosition, visualPosition))
        result.visualPositionSeconds = double (visualPosition) / viewedMap.hostRate;
    result.cuePlayheadSeconds = viewedMap.cuePlayheadSeconds;  // C の画面の Cue の時間軸
    if (viewedMap.hidden || (result.visualTimeline && result.visualTimeline->binding.key != viewedMap.key))
    {
        // 見ている役の図（V の時間軸）は外し、A の値（C・B の画面のスペクトルと範囲の帯、V の AUTO の指紋）は残す。
        // 2026-10-04 までは Tonal の計測があるか（tonalAvailable、A が鳴っていれば真）で決めていた。
        if (result.visualTimeline && result.visualTimeline->hasAData())
        {
            auto aOnly = std::make_shared<VisualTimeline> (*result.visualTimeline);
            aOnly->binding = {}; aOnly->bins.clear(); aOnly->hop = 0;
            aOnly->pairedObserving = false;
            result.visualTimeline = std::shared_ptr<const VisualTimeline> (std::move (aOnly));
        }
        else result.visualTimeline.reset();
    }
    result.separateComparisons = true;
    result.comparisonSlot = slot;
    result.audibleComparisonSlot = b.bSelected ? 1 : c.bSelected ? 2 : r.bSelected ? 3 : 0;
    result.bSelected = result.audibleComparisonSlot != 0;
    result.checkSelection = std::make_shared<const Snapshot> (c);
    result.versionSelection = std::make_shared<const Snapshot> (b);
    result.versions = b.versions;
    result.versionAuto = versionAuto;
    result.selectedVersionId = b.migratedVersionChoice == versionId && versionId.isNotEmpty()
        ? b.presetId + "/" + b.checkId + "/" + b.candidateId : versionId;
    result.versionReady = versionId.isNotEmpty() && b.sourceKind == "work_version"
        && result.selectedVersionId == b.presetId + "/" + b.checkId + "/" + b.candidateId
        && b.state == RuntimeState::ready && b.auditionBuffered;
    result.checkReady = c.state == RuntimeState::ready && c.auditionBuffered;
    // B（REF）。選んだ曲が公開され、音の準備ができていれば押してすぐ鳴る。
    result.referenceSelection = std::make_shared<const Snapshot> (r);
    result.songSets = r.songSets;
    result.songSetsIssue = r.songSetsIssue;
    result.libraryPreparation = r.libraryPreparation;
    result.selectedSongId = songId;
    result.selectedSongSetId = std::any_of (r.songSets.begin(), r.songSets.end(), [this] (const auto& set) { return set.id == songSetId; })
        ? songSetId : r.songSets.empty() ? juce::String {} : r.songSets.front().id;
    const bool songPublished = songId.isNotEmpty() && r.presetId + "/" + r.checkId + "/" + r.candidateId == songId;
    result.referenceReady = songPublished && r.state == RuntimeState::ready && r.auditionBuffered;
    result.referenceArmable = songPublished && r.playbackIdentity.isNotEmpty() && r.blindPhase == BlindPhase::inactive;
    appendPendingAudition (result, versionMap, slot == 2 ? viewedMap : check.visualBinding());
    result.blindEligible = slot == 1 && result.versionReady && b.blindEligible;
    if (slot == 1 && versionId.isEmpty())
    {
        result.state = RuntimeState::waiting;
        result.rejectionCode = "reference_version_unselected";
        result.blindEligible = false;
    }
    return result;
}

// V の Version を選ぶ。V が鳴っていた（戻る保留・押した後の待ちを含む）なら、新しい Version が公開され次第、
// 新しい MATCH で V のまま鳴らす。ほかの役（B・C）は止めない。
// automatic（V の自動特定の AUTO）は、V を選んでいないときに V の選択だけを替える。V が鳴っている・戻る保留・
// 押した後の待ちがあるときは何もしない（利用者の選択を崩さない）。
bool ReferenceComparisonController::selectVersion (const juce::String& id, bool automatic)
{
    if (trialActive()) return false;
    if (automatic)
    {
        if (version.hasOutputPath() || normalOutputSlot.load (std::memory_order_acquire) == 1
            || pendingSlot() == 1) return false;
        const juce::ScopedLock lock (selectionLock);
        if (versionId.isNotEmpty() && ! versionAuto) return false;  // 利用者が選んだ Version は替えない
    }
    if (! version.selectLibraryVersion (id)) return false;
    { const juce::ScopedLock lock (selectionLock); versionId = id; versionAuto = automatic; }
    versionChosen.store (id.isNotEmpty(), std::memory_order_release);
    if (! automatic) continueAfterSwitch (1);
    setPresented (true);
    return true;
}

// C の選択（CHECK SET・Check・曲・Cue）。C だけを替える：C が鳴っていた（戻る保留・押した後の待ちを含む）
// なら、新しい選択が公開され次第、新しい MATCH で C のまま鳴らす。ほかの役（B・V）は止めない。
// V を見ているとき（V の画面の CHECK SET は C と共用）は V の画面のまま。ほかは C の画面にする。
bool ReferenceComparisonController::selectCheckRole (const std::function<bool()>& apply)
{
    if (trialActive()) return false;
    const auto before = check.requestedGeneration();
    if (! apply()) return false;
    if (viewedSlot.load (std::memory_order_acquire) != 1) viewedSlot.store (2, std::memory_order_release);
    // 同じ選択（世代が進まない）は鳴らしたまま。Kirin OS の準備を待つ CHECK SET は C を止めて手放す。
    const auto after = check.requestedGeneration();
    if (after != before) continueAfterSwitch (2);
    else if (check.snapshot().presetSelectionStatus == "pending") continueAfterSwitch (2, false);
    return true;
}
bool ReferenceComparisonController::selectPreset (const juce::String& id)
{ return selectCheckRole ([&] { return check.selectPreset (id); }); }
bool ReferenceComparisonController::selectCheck (const juce::String& id)
{ return selectCheckRole ([&] { return id.containsChar ('/') ? check.selectLibraryCheck (id) : check.selectCheck (id); }); }
bool ReferenceComparisonController::selectCandidate (const juce::String& id)
{ return selectCheckRole ([&] { return check.selectCandidate (id); }); }
bool ReferenceComparisonController::selectCue (const juce::String& id)
{ return selectCheckRole ([&] { return check.selectCue (id); }); }
bool ReferenceComparisonController::selectVisualSlot (int slot)
{
    if ((slot != 1 && slot != 2 && slot != 3) || trialActive()) return false;
    viewedSlot.store (slot, std::memory_order_release);
    if (stateChanged) stateChanged();
    return true;
}
bool ReferenceComparisonController::retryPresetSelection() { return check.retryPresetSelection(); }
bool ReferenceComparisonController::retryCandidatePreparation() { return viewed().retryCandidatePreparation(); }
bool ReferenceComparisonController::requestRecovery() { return viewed().requestRecovery(); }

}
