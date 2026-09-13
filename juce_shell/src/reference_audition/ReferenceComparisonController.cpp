#include "ReferenceComparisonController.h"
#include <algorithm>

namespace hypha::reference_audition
{
ReferenceComparisonController::ReferenceComparisonController (juce::File root, SelectionGate callback)
    : gate (std::move (callback)),
      version (root, [this] (bool active) { return admit (1, active); }),
      check (root, [this] (bool active) { return admit (2, active); }) {}

ReferenceComparisonController::~ReferenceComparisonController() { suspendAudition(); }

bool ReferenceComparisonController::admit (int slot, bool active)
{
    const juce::ScopedLock lock (gateLock);
    if (active)
    {
        if (gateOwner != 0 || (gate && ! gate (true))) return false;
        gateOwner = slot;
    }
    else if (gateOwner == slot)
    {
        if (gate) gate (false);
        gateOwner = 0;
    }
    return true;
}

void ReferenceComparisonController::configure (RuntimeIdentity identity, double rate, int channels)
{
    {
        const juce::ScopedLock lock (selectionLock);
        if (receiverId != identity.runtimeInstanceId) versionId.clear();
        receiverId = identity.runtimeInstanceId;
    }
    auto bIdentity = identity;
    bIdentity.runtimeInstanceId += ".version";
    version.configure (bIdentity, rate, channels);
    check.configure (identity, rate, channels);
    rtPlaying = false;
    rtInputAllowed = false;
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
    result.separateComparisons = true;
    result.comparisonSlot = slot;
    result.audibleComparisonSlot = b.bSelected ? 1 : c.bSelected ? 2 : 0;
    result.checkSelection = std::make_shared<const Snapshot> (c);
    result.versions = b.versions;
    result.selectedVersionId = versionId;
    result.versionReady = versionId.isNotEmpty() && b.sourceKind == "work_version"
        && versionId == b.presetId + "/" + b.checkId + "/" + b.candidateId
        && b.state == RuntimeState::ready && b.auditionBuffered;
    result.checkReady = c.state == RuntimeState::ready && c.auditionBuffered;
    result.blindEligible = slot == 1 && result.versionReady && b.blindEligible;
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
    viewedSlot.store (1, std::memory_order_release);
    return true;
}

bool ReferenceComparisonController::selectPreset (const juce::String& id)
{
    if (trialActive()) return false;
    selectA(); viewedSlot.store (2, std::memory_order_release);
    return check.selectPreset (id);
}
bool ReferenceComparisonController::selectCheck (const juce::String& id)
{
    if (trialActive()) return false;
    selectA(); viewedSlot.store (2, std::memory_order_release);
    return id.containsChar ('/') ? check.selectLibraryCheck (id) : check.selectCheck (id);
}
bool ReferenceComparisonController::selectCandidate (const juce::String& id)
{
    if (trialActive()) return false;
    selectA(); viewedSlot.store (2, std::memory_order_release);
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
    return version.selectB (loudness, peak);
}
bool ReferenceComparisonController::selectC (double loudness, double peak) noexcept
{
    if (trialActive() || ! snapshot().checkReady) return false;
    version.selectA();
    viewedSlot.store (2, std::memory_order_release);
    return check.selectB (loudness, peak);
}
void ReferenceComparisonController::selectA() noexcept { version.selectA(); check.selectA(); }
bool ReferenceComparisonController::startBlind (double loudness, double peak) noexcept
{
    if (! snapshot().versionReady) return false;
    check.selectA(); viewedSlot.store (1, std::memory_order_release);
    return version.startBlind (loudness, peak);
}
bool ReferenceComparisonController::approveBlindLowerAAndStart (double loudness, double peak) noexcept
{
    check.selectA(); viewedSlot.store (1, std::memory_order_release);
    return version.approveBlindLowerAAndStart (loudness, peak);
}
bool ReferenceComparisonController::selectBlindStimulus (int value) noexcept { return version.selectBlindStimulus (value); }
bool ReferenceComparisonController::answerBlind (int value) noexcept { return version.answerBlind (value); }
bool ReferenceComparisonController::revealBlind() noexcept { return version.revealBlind(); }
void ReferenceComparisonController::endBlind() noexcept { version.endBlind(); }
void ReferenceComparisonController::suspendAudition() noexcept { version.suspendAudition(); check.suspendAudition(); }

void ReferenceComparisonController::observeTransport (std::int64_t position, bool valid, bool playing) noexcept
{
    rtPlaying = playing;
    version.observeTransport (position, valid, playing);
    check.observeTransport (position, valid, playing);
}
void ReferenceComparisonController::observeAInput (const juce::AudioBuffer<float>& buffer,
    std::int64_t position, bool valid, bool playing, bool allowed) noexcept
{
    rtInputAllowed = allowed;
    version.observeAInput (buffer, position, valid, playing, allowed, false);
    check.observeAInput (buffer, position, valid, playing, allowed, false);
}
bool ReferenceComparisonController::renderSelectedB (juce::AudioBuffer<float>& buffer,
    std::int64_t position, bool valid, bool allowed, bool returnAllowed) noexcept
{
    const bool rendered = viewed().renderSelectedB (buffer, position, valid, allowed, returnAllowed);
    // Both journals observe A only after the actual output decision. C->B is not an A return.
    if (! rendered && rtInputAllowed && rtPlaying && valid && buffer.getNumSamples() > 0)
    {
        version.confirmAOutput();
        check.confirmAOutput();
    }
    return rendered;
}
}
