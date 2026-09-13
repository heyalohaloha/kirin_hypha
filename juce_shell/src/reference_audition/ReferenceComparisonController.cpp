#include "ReferenceComparisonController.h"
#include <algorithm>

namespace hypha::reference_audition
{
ReferenceComparisonController::ReferenceComparisonController (juce::File root, SelectionGate callback, SelectionGate captureCallback, SelectionGate blindCallback)
    : gate (std::move (callback)), captureGate(std::move(captureCallback)), blindCaptureGate(std::move(blindCallback)),
      version (root, [this] (bool active) { return admit (1, active); }, true),
      check (root, [this] (bool active) { return admit (2, active); }),
      visual ([this] {
          auto result = version.visualBinding();
          result.hidden = result.hidden || viewedSlot.load (std::memory_order_acquire) != 1;
          return result;
      }), capture([this](bool active){return admitCapture(active);}, [this]{return captureReceipt();}),
      captureProjection(capture.access,[this]{return version.visualBinding();}) {}

ReferenceComparisonController::~ReferenceComparisonController()
{
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
        if (gateOwners == 0)
        {
            visual.pauseAdmission(); capture.pauseObservation();
            const bool admitted = !gate || gate (true);
            if(!captureOwned) visual.useAuditionAdmission (admitted);
            capture.useAuditionAdmission(admitted);
            if (!admitted) return false;
        }
        gateOwners |= bit;
    }
    else if ((gateOwners & bit) != 0)
    {
        gateOwners &= ~bit;
        if(slot==1 && blindGuardOwned) { if(blindCaptureGate) blindCaptureGate(false); blindGuardOwned=false; }
        if (gateOwners == 0)
        {
            visual.pauseAdmission(); capture.pauseObservation();
            if (gate) gate (false);
            if(!captureOwned) visual.useAuditionAdmission (false);
            capture.useAuditionAdmission(false);
        }
    }
    return true;
}

void ReferenceComparisonController::configure (RuntimeIdentity identity, double rate, int channels)
{
    {
        const juce::ScopedLock lock (selectionLock);
        if (receiverId != identity.runtimeInstanceId && ! pendingSettings) versionId.clear();
        receiverId = identity.runtimeInstanceId;
        versionChosen.store (versionId.isNotEmpty(), std::memory_order_release);
    }
    capture.configure(identity.runtimeInstanceId,rate,channels);
    auto bIdentity = identity;
    bIdentity.runtimeInstanceId += ".version";
    version.configure (bIdentity, rate, channels);
    check.configure (identity, rate, channels);
    std::optional<ReferenceComparisonSettings> pending;
    { const juce::ScopedLock lock (selectionLock); configured = true; pending = pendingSettings; }
    if (pending) restoreSettings (*pending);
    rtPlaying = false;
    rtInputAllowed = false;
}

ReferenceComparisonSettings ReferenceComparisonController::savedSettings() const
{
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
    result.check = check.savedChoice();
    result.visualView = visualPreferences->get();
    result.captureState = capture.access->snapshot().encoded; result.capturedView = capture.access->capturedView;
    result.viewedSlot = viewedSlot.load (std::memory_order_acquire);
    return result;
}

void ReferenceComparisonController::restoreSettings (const ReferenceComparisonSettings& input)
{
    ReferenceComparisonSettings value = input;
    visualPreferences->set (value.visualView);
    if (! value.version.valid() || value.version.candidateId.isEmpty()) value.version = {};
    if (! value.check.valid()) value.check = {};
    selectA();
    bool apply = false;
    {
        const juce::ScopedLock lock (selectionLock);
        versionId = value.version.candidateId.isEmpty() ? juce::String {} : value.version.target();
        versionChosen.store (versionId.isNotEmpty(), std::memory_order_release);
        viewedSlot.store (value.viewedSlot == 1 ? 1 : 2, std::memory_order_release);
        apply = configured;
        pendingSettings = apply ? std::optional<ReferenceComparisonSettings> {} : value;
    }
    if (apply) { version.restoreChoice (value.version); check.restoreChoice (value.check); capture.restore(value.captureState,value.capturedView); }
}

bool ReferenceComparisonController::trialActive() const
{
    return version.snapshot().blindPhase != BlindPhase::inactive;
}

RuntimeV2Controller& ReferenceComparisonController::viewed() noexcept
{
    return viewedSlot.load (std::memory_order_acquire) == 1 ? version : check;
}

Snapshot ReferenceComparisonController::snapshot() const
{
    const auto b = version.snapshot(), c = check.snapshot();
    const juce::ScopedLock lock (selectionLock);
    const auto slot = viewedSlot.load (std::memory_order_acquire);
    auto result = slot == 1 ? b : c;
    result.visualTimeline = visual.snapshot();
    result.visualPreferences = visualPreferences;
    const auto map = version.visualBinding();
    std::int64_t visualPosition = 0;
    if (map.aligned && !map.hidden && map.hostPositionValid && map.hostRate > 0 && map.mapPosition (map.hostPosition, visualPosition))
        result.visualPositionSeconds = double (visualPosition) / map.hostRate;
    if (map.hidden || !map.source || (result.visualTimeline && result.visualTimeline->binding.key != map.key))
        result.visualTimeline.reset();
    result.captureAccess=capture.access;
    if(capture.access->capturedView && !trialActive())
    { result.visualTimeline=captureProjection.snapshot(); result.visualPositionSeconds=result.visualTimeline && result.visualTimeline->capture && map.hostPositionValid
        ? double(map.hostPosition-result.visualTimeline->capture->hostStart)/result.visualTimeline->capture->rate : -1; }
    result.separateComparisons = true;
    result.comparisonSlot = slot;
    result.audibleComparisonSlot = b.bSelected ? 1 : c.bSelected ? 2 : 0;
    result.checkSelection = std::make_shared<const Snapshot> (c);
    result.versions = b.versions;
    result.selectedVersionId = b.migratedVersionChoice == versionId && versionId.isNotEmpty()
        ? b.presetId + "/" + b.checkId + "/" + b.candidateId : versionId;
    result.versionReady = versionId.isNotEmpty() && b.sourceKind == "work_version"
        && result.selectedVersionId == b.presetId + "/" + b.checkId + "/" + b.candidateId
        && b.state == RuntimeState::ready && b.auditionBuffered;
    result.checkReady = c.state == RuntimeState::ready && c.auditionBuffered;
    result.blindEligible = slot == 1 && result.versionReady && b.blindEligible && !capture.access->active;
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
    if (trialActive() || ! version.selectLibraryVersion (id)) return false;
    selectA();
    { const juce::ScopedLock lock (selectionLock); versionId = id; }
    versionChosen.store (id.isNotEmpty(), std::memory_order_release);
    setPresented (true);
    viewedSlot.store (1, std::memory_order_release);
    return true;
}

bool ReferenceComparisonController::selectPreset (const juce::String& id)
{
    if (trialActive()) return false;
    selectA(); capture.access->capturedView=false; viewedSlot.store (2, std::memory_order_release);
    return check.selectPreset (id);
}
bool ReferenceComparisonController::selectCheck (const juce::String& id)
{
    if (trialActive()) return false;
    selectA(); capture.access->capturedView=false; viewedSlot.store (2, std::memory_order_release);
    return id.containsChar ('/') ? check.selectLibraryCheck (id) : check.selectCheck (id);
}
bool ReferenceComparisonController::selectCandidate (const juce::String& id)
{
    if (trialActive()) return false;
    selectA(); capture.access->capturedView=false; viewedSlot.store (2, std::memory_order_release);
    return check.selectCandidate (id);
}
bool ReferenceComparisonController::selectCue (const juce::String& id)
{
    if (trialActive()) return false;
    selectA(); return viewed().selectCue (id);
}
bool ReferenceComparisonController::retryPresetSelection() { return check.retryPresetSelection(); }
bool ReferenceComparisonController::retryCandidatePreparation() { return viewed().retryCandidatePreparation(); }
bool ReferenceComparisonController::approveSampleRateConversion() { return viewed().approveSampleRateConversion(); }
bool ReferenceComparisonController::requestRecovery() { return viewed().requestRecovery(); }

bool ReferenceComparisonController::selectB (double loudness, double peak) noexcept
{
    if (trialActive() || ! snapshot().versionReady) return false;
    check.selectA();
    viewedSlot.store (1, std::memory_order_release);
    const bool selected = version.selectB (loudness, peak);
    normalOutputSlot.store (selected ? 1 : 0, std::memory_order_release);
    return selected;
}
bool ReferenceComparisonController::selectC (double loudness, double peak) noexcept
{
    if (trialActive() || ! snapshot().checkReady) return false;
    version.selectA(); capture.access->capturedView=false;
    viewedSlot.store (2, std::memory_order_release);
    const bool selected = check.selectB (loudness, peak);
    normalOutputSlot.store (selected ? 2 : 0, std::memory_order_release);
    return selected;
}
void ReferenceComparisonController::selectA() noexcept
{ normalOutputSlot.store (0, std::memory_order_release); version.selectA(); check.selectA(); }
bool ReferenceComparisonController::startBlind (double loudness, double peak) noexcept
{
    if (trialActive() || ! snapshot().versionReady || !beginBlindGuard()) return false;
    check.suspendAudition(); version.selectA(); viewedSlot.store (1, std::memory_order_release);
    const bool started=version.startBlind(loudness,peak); if(!started) endBlindGuard(); return started;
}
bool ReferenceComparisonController::approveBlindLowerAAndStart (double loudness, double peak) noexcept
{
    if (trialActive() || !snapshot().versionReady || !beginBlindGuard()) return false;
    check.suspendAudition(); version.selectA(); viewedSlot.store (1, std::memory_order_release);
    const bool started=version.approveBlindLowerAAndStart(loudness,peak); if(!started) endBlindGuard(); return started;
}
bool ReferenceComparisonController::selectBlindStimulus (int value) noexcept { return version.selectBlindStimulus (value); }
bool ReferenceComparisonController::answerBlind (int value) noexcept { return version.answerBlind (value); }
bool ReferenceComparisonController::revealBlind() noexcept { return version.revealBlind(); }
void ReferenceComparisonController::endBlind() noexcept { version.endBlind(); endBlindGuard(); }
void ReferenceComparisonController::suspendAudition() noexcept { version.suspendAudition(); check.suspendAudition(); }

void ReferenceComparisonController::observeTransport (std::int64_t position, bool valid, bool playing) noexcept
{
    rtPlaying = playing;
    version.observeTransport (position, valid, playing);
    check.observeTransport (position, valid, playing);
}
void ReferenceComparisonController::observeAInput (const juce::AudioBuffer<float>& buffer,
    std::int64_t position, bool valid, bool playing, bool allowed, int clock, std::optional<bool> captureAllowed) noexcept
{
    rtInputAllowed = allowed;
    if(!capture.observe(buffer,position,valid,playing,captureAllowed.value_or(allowed),clock))
        visual.observe (buffer, position, valid && playing && allowed && versionChosen.load (std::memory_order_acquire));
    version.observeAInput (buffer, position, valid, playing, allowed && versionChosen.load (std::memory_order_acquire), false);
    check.observeAInput (buffer, position, valid, playing, allowed, false);
}
bool ReferenceComparisonController::renderSelectedB (juce::AudioBuffer<float>& buffer,
    std::int64_t position, bool valid, bool allowed, bool returnAllowed) noexcept
{
    const int target = normalOutputSlot.load (std::memory_order_acquire);
    const bool bPath = version.hasOutputPath(), cPath = check.hasOutputPath();
    bool rendered = false;
    if (bPath && cPath)
    {
        const int frames = buffer.getNumSamples(), channels = buffer.getNumChannels();
        if (frames < 1 || frames > 8192 || channels < 1 || channels > 2)
        { version.renderSelectedB (buffer, position, valid, false, false); check.renderSelectedB (buffer, position, valid, false, false); return false; }
        for (int c = 0; c < channels; ++c)
        { bScratch.copyFrom (c, 0, buffer, c, 0, frames); cScratch.copyFrom (c, 0, buffer, c, 0, frames); }
        juce::AudioBuffer<float> b (bScratch.getArrayOfWritePointers(), channels, frames);
        juce::AudioBuffer<float> c (cScratch.getArrayOfWritePointers(), channels, frames);
        const bool renderedB = version.renderSelectedB (b, position, valid, allowed, returnAllowed, target == 1);
        const bool renderedC = check.renderSelectedB (c, position, valid, allowed, returnAllowed, target == 2);
        rendered = renderedB || renderedC;
        if (rendered) for (int channel = 0; channel < channels; ++channel)
            for (int frame = 0; frame < frames; ++frame)
                buffer.setSample (channel, frame, renderedB && renderedC
                    ? b.getSample (channel, frame) + (c.getSample (channel, frame) - buffer.getSample (channel, frame))
                    : (renderedB ? b : c).getSample (channel, frame));
    }
    else if (bPath) rendered = version.renderSelectedB (buffer, position, valid, allowed, returnAllowed, target == 1);
    else if (cPath) rendered = check.renderSelectedB (buffer, position, valid, allowed, returnAllowed, target == 2);
    // Both journals observe A only after the actual output decision. C->B is not an A return.
    if (! rendered && rtInputAllowed && rtPlaying && valid && buffer.getNumSamples() > 0)
    {
        version.confirmAOutput();
        check.confirmAOutput();
    }
    return rendered;
}
}
