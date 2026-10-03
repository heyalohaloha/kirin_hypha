#include "ReferenceComparisonController.h"
#include <algorithm>

namespace hypha::reference_audition
{
ReferenceComparisonController::ReferenceComparisonController (juce::File root, SelectionGate callback,
    SelectionGate captureCallback, SelectionGate blindCallback, StateChanged stateChangedIn)
    : gate (std::move (callback)), captureGate(std::move(captureCallback)), blindCaptureGate(std::move(blindCallback)),
      stateChanged(std::move(stateChangedIn)),
      version (root, [this] (bool active) { return admit (1, active); }, true),
      check (root, [this] (bool active) { return admit (2, active); }, false,
          [inbox = std::weak_ptr<WorkflowCommitInbox> (workflowCommitInbox)] (const WorkflowEventCommit& commit)
          {
              const auto target = inbox.lock();
              if (target == nullptr) return;
              const juce::ScopedLock lock (target->lock);
              if (! target->accepting) return;
              for (const auto& queued : target->commits)
                  if (queued.operationId == commit.operationId) return;
              if (target->commits.size() < 32)
              {
                  target->commits.push_back (commit);
                  if (target->updater != nullptr) target->updater->triggerAsyncUpdate();
              }
          }),
      reference (root, [this] (bool active) { return admit (3, active); }, false),
      visual ([this] {
          return slotController (viewedSlot.load (std::memory_order_acquire)).visualBinding();
      },analysis,root), capture([this](bool active){return admitCapture(active);},
          [this]{return captureReceipt();},analysis,&visual,root),
      captureProjection(capture.access,[this]{return version.visualBinding();},analysis,
          [this]{const juce::ScopedLock lock(selectionLock);return tonalState;},root,
          [this]{return check.visualBinding();})
{
    reference.setTrackingEnabled (true);  // H8・H3: B は A の直近 10 秒に追従する
    reference.setSongsOnly (true);        // B は B セットの曲だけを鳴らす（C の Preset に落ちない）
    const juce::ScopedLock inboxLock (workflowCommitInbox->lock);
    workflowCommitInbox->updater = this;
}

ReferenceComparisonController::~ReferenceComparisonController()
{
    {
        const juce::ScopedLock inboxLock (workflowCommitInbox->lock);
        workflowCommitInbox->accepting = false;
        workflowCommitInbox->updater = nullptr;
        workflowCommitInbox->commits.clear();
    }
    cancelPendingUpdate();
    const juce::ScopedLock serviceLock (workflowServiceLock);
    setPresented (false); capture.shutdown(); suspendAudition();
    const juce::ScopedLock lock (gateLock); closing = true;
    visual.pauseAdmission(); if (gateOwners && gate) gate (false); gateOwners = 0;
    if(blindGuardOwned && blindCaptureGate) blindCaptureGate(false); blindGuardOwned=false;
}

void ReferenceComparisonController::setPresented (bool active) noexcept
{ presented=active; refreshObservation(); }

bool ReferenceComparisonController::admit (int slot, bool active)
{
    const juce::ScopedLock lock (gateLock);
    if (closing) return !active;
    const int bit = 1 << slot;
    if (active)
    {
        if ((gateOwners & bit) != 0) return false;
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
        if(slot==1 && blindGuardOwned) { if(blindCaptureGate) blindCaptureGate(false); blindGuardOwned=false; capture.access->releaseBlind(CaptureBlindOwner::version); }
        if (gateOwners == 0)
        {
            if (gate) gate (false);
        }
    }
    return true;
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
    capture.configure(identity.runtimeInstanceId,rate,channels);
    visual.configure(rate,channels);
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
    serviceWorkflowCommits();
    const auto b = version.snapshot();
    const juce::ScopedLock lock (selectionLock);
    if (pendingSettings) return *pendingSettings;
    ReferenceComparisonSettings result;
    result.version = versionId.isEmpty() ? ReferenceChoice {} : version.savedChoice();
    // A removed Version must not be replaced by a fallback selection on save.
    if (versionId.isNotEmpty() && b.migratedVersionChoice != versionId)
    {
        const auto ids = juce::StringArray::fromTokens (versionId, "/", {});
        if (ids.size() == 3)
        { result.version.presetId = ids[0]; result.version.checkId = ids[1]; result.version.candidateId = ids[2]; }
    }
    result.check = activeWorkflow != nullptr ? normalCheckChoice : check.savedChoice();
    result.reference = songId.isEmpty() ? ReferenceChoice {} : reference.savedChoice();
    result.songSetId = songSetId;
    result.visualView = visualPreferences->get();
    result.captureState = capture.access->store.value().encoded; result.capturedView = capture.access->capturedView;
    result.tonal = tonalState;
    result.workflow = workflowState;
    result.viewedSlot = viewedSlot.load (std::memory_order_acquire);
    return result;
}

void ReferenceComparisonController::setCaptureTonalRange(double startSeconds,double endSeconds)
{
    const auto state=capture.access->snapshot();
    if(!state.shown||!state.shown->tonal.valid())return;
    TonalDisplayState next;
    next.source=TonalDisplayState::Source::captured;
    next.captureId=state.shown->id;
    next.artifactSha256=state.shown->tonal.artifactSha256;
    const auto duration=state.shown->duration();
    if(std::isfinite(startSeconds)&&std::isfinite(endSeconds)&&startSeconds>=0
        &&endSeconds>startSeconds&&endSeconds<=duration)
    {next.rangeStart=startSeconds;next.rangeEnd=endSeconds;}
    {
        const juce::ScopedLock lock(selectionLock);
        tonalState=next;
    }
    if(stateChanged)stateChanged();
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
        versionChosen.store (versionId.isNotEmpty(), std::memory_order_release);
        viewedSlot.store (value.viewedSlot == 1 || value.viewedSlot == 3 ? value.viewedSlot : 2, std::memory_order_release);
        songId = value.reference.candidateId.isEmpty() ? juce::String {} : value.reference.target();
        songSetId = value.songSetId;
        tonalState = value.tonal;
        workflowState = value.workflow;
        normalCheckChoice = value.check;
        activeWorkflow.reset();
        pendingWorkflowTransition.reset();
        workflowItemIndex = 0;
        apply = configured;
        pendingSettings = apply ? std::optional<ReferenceComparisonSettings> {} : value;
    }
    if (apply) { version.restoreChoice (value.version); check.restoreChoice (value.check); reference.restoreChoice (value.reference);
                 capture.restore(value.captureState,value.capturedView); }
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
    serviceWorkflowCommits();
    const auto b = version.snapshot(), c = check.snapshot(), r = reference.snapshot();
    ensureReferenceSong (r);
    const juce::ScopedLock lock (selectionLock);
    const auto slot = viewedSlot.load (std::memory_order_acquire);
    auto result = slot == 1 ? b : slot == 3 ? r : c;
    result.visualTimeline = visual.snapshot();
    result.visualPreferences = visualPreferences;
    const auto viewedMap = slotController (slot).visualBinding();
    const auto versionMap = version.visualBinding();
    std::int64_t visualPosition = 0;
    if (viewedMap.aligned && !viewedMap.hidden && viewedMap.hostPositionValid && viewedMap.hostRate > 0
        && viewedMap.mapPosition (viewedMap.hostPosition, visualPosition))
        result.visualPositionSeconds = double (visualPosition) / viewedMap.hostRate;
    if (viewedMap.hidden || (result.visualTimeline && result.visualTimeline->binding.key != viewedMap.key))
    {
        if (result.visualTimeline && result.visualTimeline->tonalAvailable)
        {
            auto tonalOnly = std::make_shared<VisualTimeline> (*result.visualTimeline);
            tonalOnly->binding = {}; tonalOnly->bins.clear(); tonalOnly->hop = 0;
            tonalOnly->pairedObserving = false;
            result.visualTimeline = std::shared_ptr<const VisualTimeline> (std::move (tonalOnly));
        }
        else result.visualTimeline.reset();
    }
    result.captureAccess=capture.access;
    const auto captureState=capture.access->snapshot();
    if(capture.access->capturedView && !trialActive())
    { result.visualTimeline=captureProjection.snapshot();
      if(result.visualTimeline && result.visualTimeline->capture
          && (!captureState.shown || result.visualTimeline->capture->id!=captureState.shown->id
              || (result.visualTimeline->binding.source && (!versionMap.source
                  || result.visualTimeline->binding.source->sourceFileSha256!=versionMap.source->sourceFileSha256)))) result.visualTimeline.reset();
      result.visualPositionSeconds=result.visualTimeline && result.visualTimeline->capture && versionMap.hostPositionValid && captureState.timingVerified
            && captureState.confirmedTimingEpoch==capture.access->currentTimingEpoch.load()
        ? double(versionMap.hostPosition-result.visualTimeline->capture->hostStart)/result.visualTimeline->capture->rate : -1; }
    result.separateComparisons = true;
    result.comparisonSlot = slot;
    result.audibleComparisonSlot = b.bSelected ? 1 : c.bSelected ? 2 : r.bSelected ? 3 : 0;
    result.bSelected = result.audibleComparisonSlot != 0;
    result.checkSelection = std::make_shared<const Snapshot> (c);
    result.versionSelection = std::make_shared<const Snapshot> (b);
    result.versions = b.versions;
    result.selectedVersionId = b.migratedVersionChoice == versionId && versionId.isNotEmpty()
        ? b.presetId + "/" + b.checkId + "/" + b.candidateId : versionId;
    result.versionReady = versionId.isNotEmpty() && b.sourceKind == "work_version"
        && result.selectedVersionId == b.presetId + "/" + b.checkId + "/" + b.candidateId
        && b.state == RuntimeState::ready && b.auditionBuffered;
    result.checkReady = c.state == RuntimeState::ready && c.auditionBuffered;
    // H8: B（REF）。選んだ曲が公開され、音の準備ができていれば押してすぐ鳴る。
    result.referenceSelection = std::make_shared<const Snapshot> (r);
    result.songSets = r.songSets;
    result.selectedSongId = songId;
    result.selectedSongSetId = std::any_of (r.songSets.begin(), r.songSets.end(), [this] (const auto& set) { return set.id == songSetId; })
        ? songSetId : r.songSets.empty() ? juce::String {} : r.songSets.front().id;
    const bool songPublished = songId.isNotEmpty() && r.presetId + "/" + r.checkId + "/" + r.candidateId == songId;
    result.referenceReady = songPublished && r.state == RuntimeState::ready && r.auditionBuffered;
    result.referenceArmable = songPublished && r.playbackIdentity.isNotEmpty() && r.blindPhase == BlindPhase::inactive;
    appendPendingAudition (result, versionMap, slot == 2 ? viewedMap : check.visualBinding());
    const auto catalog = c.workflowCatalog;
    result.workflow.reviewAvailable = catalog != nullptr && catalog->latestReview != nullptr;
    result.workflow.bookmarkAvailable = catalog != nullptr && catalog->latestBookmark != nullptr;
    if (activeWorkflow != nullptr && workflowItemIndex >= 0
        && workflowItemIndex < static_cast<int> (activeWorkflow->items.size()))
    {
        const auto& item = activeWorkflow->items[static_cast<size_t> (workflowItemIndex)];
        result.workflow.mode = activeWorkflow->kind == WorkflowDefinition::Kind::review
            ? WorkflowView::Mode::review : WorkflowView::Mode::bookmark;
        result.workflow.definitionId = activeWorkflow->id;
        result.workflow.definitionTitle = activeWorkflow->title;
        result.workflow.itemId = item.itemId;
        result.workflow.itemTitle = item.title;
        result.workflow.purpose = item.purpose;
        result.workflow.itemIndex = workflowItemIndex;
        result.workflow.itemCount = static_cast<int> (activeWorkflow->items.size());
        result.workflow.canMoveBack = workflowItemIndex > 0;
        result.workflow.canAdvance = true;
        result.workflow.canEnd = true;
        const auto token = activeWorkflow->revisionId + ":" + item.itemId;
        if (pendingWorkflowTransition.has_value())
            result.workflow.status = WorkflowView::Status::saving;
        else if (c.workflowToken == token && c.state == RuntimeState::ready)
            result.workflow.status = WorkflowView::Status::ready;
        else if (c.state == RuntimeState::rejected && c.rejectionCode == "reference_workflow_condition_changed")
        {
            result.workflow.status = WorkflowView::Status::rejected;
            result.workflow.message = "SOURCE OR CUE CHANGED";
        }
        else result.workflow.status = WorkflowView::Status::preparing;
    }
    else if (workflowState.mode != WorkflowResumeState::Mode::idle)
    {
        result.workflow.status = WorkflowView::Status::resumeAvailable;
        result.workflow.mode = workflowState.mode == WorkflowResumeState::Mode::review
            ? WorkflowView::Mode::review : WorkflowView::Mode::bookmark;
        result.workflow.definitionId = workflowState.mode == WorkflowResumeState::Mode::review
            ? workflowState.reviewId : workflowState.bookmarkId;
        result.workflow.message = "CONTINUE WHEN READY";
    }
    else result.workflow.status = result.workflow.reviewAvailable || result.workflow.bookmarkAvailable
        ? WorkflowView::Status::available : WorkflowView::Status::unavailable;
    result.blindEligible = slot == 1 && result.versionReady && b.blindEligible && !capture.access->busy();
    if (slot == 1 && versionId.isEmpty())
    {
        result.state = RuntimeState::waiting;
        result.rejectionCode = "reference_version_unselected";
        result.blindEligible = false;
    }
    return result;
}

bool ReferenceComparisonController::selectVersion (const juce::String& id)
{
    serviceWorkflowCommits();
    if (trialActive()) return false;
    if (hasActiveWorkflow())
    {
        if (! finishWorkflow (false)) return false;
        const juce::ScopedLock lock (selectionLock);
        if (activeWorkflow != nullptr) return false;
    }
    if (! version.selectLibraryVersion (id)) return false;
    selectA();
    { const juce::ScopedLock lock (selectionLock); versionId = id; }
    versionChosen.store (id.isNotEmpty(), std::memory_order_release);
    setPresented (true);
    return true;
}

bool ReferenceComparisonController::selectPreset (const juce::String& id)
{
    serviceWorkflowCommits();
    if (trialActive()) return false;
    if (hasActiveWorkflow())
    {
        if (! finishWorkflow (false)) return false;
        const juce::ScopedLock lock (selectionLock);
        if (activeWorkflow != nullptr) return false;
    }
    selectA(); capture.access->capturedView=false; viewedSlot.store (2, std::memory_order_release);
    return check.selectPreset (id);
}
bool ReferenceComparisonController::selectCheck (const juce::String& id)
{
    serviceWorkflowCommits();
    if (trialActive()) return false;
    if (hasActiveWorkflow())
    {
        if (! finishWorkflow (false)) return false;
        const juce::ScopedLock lock (selectionLock);
        if (activeWorkflow != nullptr) return false;
    }
    selectA(); capture.access->capturedView=false; viewedSlot.store (2, std::memory_order_release);
    return id.containsChar ('/') ? check.selectLibraryCheck (id) : check.selectCheck (id);
}
bool ReferenceComparisonController::selectCandidate (const juce::String& id)
{
    serviceWorkflowCommits();
    if (trialActive()) return false;
    if (hasActiveWorkflow())
    {
        if (! finishWorkflow (false)) return false;
        const juce::ScopedLock lock (selectionLock);
        if (activeWorkflow != nullptr) return false;
    }
    selectA(); capture.access->capturedView=false; viewedSlot.store (2, std::memory_order_release);
    return check.selectCandidate (id);
}
bool ReferenceComparisonController::selectCue (const juce::String& id)
{
    serviceWorkflowCommits();
    if (trialActive()) return false;
    if (hasActiveWorkflow())
    {
        if (! finishWorkflow (false)) return false;
        const juce::ScopedLock lock (selectionLock);
        if (activeWorkflow != nullptr) return false;
    }
    selectA(); return check.selectCue (id);
}
bool ReferenceComparisonController::selectVisualSlot (int slot)
{
    if ((slot != 1 && slot != 2 && slot != 3) || trialActive() || hasActiveWorkflow()) return false;
    viewedSlot.store (slot, std::memory_order_release);
    capture.access->capturedView = false;
    if (stateChanged) stateChanged();
    return true;
}
bool ReferenceComparisonController::retryPresetSelection() { return check.retryPresetSelection(); }
bool ReferenceComparisonController::retryCandidatePreparation() { return viewed().retryCandidatePreparation(); }
bool ReferenceComparisonController::approveSampleRateConversion(int slot)
{
    if (slot == 1) return version.approveSampleRateConversion();
    if (slot == 2) return check.approveSampleRateConversion();
    return false;
}
bool ReferenceComparisonController::requestRecovery() { return viewed().requestRecovery(); }

}
