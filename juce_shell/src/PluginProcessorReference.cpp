#include "kirin_hypha_reference_capture_ffi.h"
#include "PluginProcessor.h"
#include "reference_audition/ReferenceLiveWindowLoudness.h"

#include <cmath>
#include <limits>


hypha::reference_audition::Snapshot KirinHyphaProcessorBase::referenceAuditionSnapshot() const
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController != nullptr
        ? referenceAuditionController->snapshot()
        : hypha::reference_audition::Snapshot {};
   #else
    return {};
   #endif
}

void KirinHyphaProcessorBase::setReferenceViewPresented (bool active)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController) referenceAuditionController->setPresented (active);
   #else
    juce::ignoreUnused (active);
   #endif
}

hypha::reference_audition::LiveALevel KirinHyphaProcessorBase::referenceLiveALevel (bool windowOnly, int windowBlocks) const
{
    namespace ref = hypha::reference_audition;
    KirinObservatoryFrame frame {};
    const bool received = pollObservatoryFrame (frame);
    auto level = ref::liveALevel (frame, received, heartbeatLive(), isPlaying());
    if (! std::isfinite (level.loudness)) return level;
    // H2: A の音量は直近の窓（B・V は 10 秒、C は Cue と同じ長さ）のゲートつき音量（積算の Integrated は
    // 使わない）。Peak と上限はセッションの max TP のまま。窓が 3 秒に満たないあいだ（再生を始めた直後・
    // シークの後）は、選ぶときは積算の値を使い、追従（windowOnly）では値なしにして直前の gain を保たせる。
   #if ! KIRIN_HYPHA_PRE_DISPLAY  // Reference の試聴は POST だけ（PRE は窓の計算を持たない）
    const auto blocks = static_cast<size_t> (juce::jmax (1, windowBlocks));
    std::vector<KirinMeterHistoryEntry> history;
    const auto window = pollMeterHistory (KIRIN_METER_HISTORY_10_HZ, history, blocks, blocks)
        ? ref::liveWindowLoudness (history, windowBlocks) : ref::LiveWindowLoudness {};
    if (window.gatedBlocks >= ref::liveWindowMinimumGatedBlocks && std::isfinite (window.lufs))
        level.loudness = window.lufs;
    else if (windowOnly)
        level.loudness = std::numeric_limits<double>::quiet_NaN();
   #else
    juce::ignoreUnused (windowOnly, windowBlocks);
   #endif
    return level;
}

bool KirinHyphaProcessorBase::selectReferenceB() { return requestReferenceAudition (1); }
bool KirinHyphaProcessorBase::selectReferenceC() { return requestReferenceAudition (2); }
bool KirinHyphaProcessorBase::selectReferenceRef() { return requestReferenceAudition (3); }  // H8: B（REF）

// H8: B の曲と B SET。B が鳴っていれば、新しい曲が準備でき次第 B のまま鳴る。
bool KirinHyphaProcessorBase::selectReferenceSong (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    const bool selected = licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectSong (id);
    if (selected && referencePendingAuditionNeedsService()) startTimer (50);
    return selected;
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceSongSet (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectSongSet (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::requestReferenceAudition (int slot)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    const auto level = referenceLiveALevel (false, referenceAuditionController != nullptr
        ? referenceAuditionController->liveWindowBlocks (slot) : hypha::reference_audition::liveWindowBlocks);
    const bool accepted = referenceAuditionController != nullptr
        && referenceAuditionController->requestAudition (slot, level.loudness, level.peak);
    if (accepted && (referencePendingAuditionNeedsService() || referenceTrackingNeedsService())) startTimer (50);
    return accepted;
   #else
    juce::ignoreUnused (slot);
    return false;
   #endif
}

void KirinHyphaProcessorBase::selectReferenceA()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr)
        referenceAuditionController->selectA();
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceVersion (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectVersion (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferencePreset (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectPreset (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::retryReferencePresetSelection()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->retryPresetSelection();
   #else
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceCheck (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectCheck (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceCandidate (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectCandidate (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::retryReferenceCandidatePreparation()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->retryCandidatePreparation();
   #else
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceCue (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectCue (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::startLatestReferenceReview()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->startLatestReview();
   #else
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceVisualSlot (int slot)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectVisualSlot (slot);
   #else
    juce::ignoreUnused (slot);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::startLatestReferenceBookmark()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->startLatestBookmark();
   #else
    return false;
   #endif
}

bool KirinHyphaProcessorBase::moveReferenceWorkflow (
    int direction, bool confirmed, bool deferred)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->moveWorkflow (direction, confirmed, deferred);
   #else
    juce::ignoreUnused (direction, confirmed, deferred);
    return false;
   #endif
}

void KirinHyphaProcessorBase::endReferenceWorkflow()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr) referenceAuditionController->endWorkflow();
   #endif
}

void KirinHyphaProcessorBase::setReferenceCaptureTonalRange (
    double startSeconds, double endSeconds)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr)
        referenceAuditionController->setCaptureTonalRange (startSeconds, endSeconds);
   #else
    juce::ignoreUnused (startSeconds, endSeconds);
   #endif
}

bool KirinHyphaProcessorBase::approveReferenceSampleRateConversion(int slot)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->approveSampleRateConversion(slot);
   #else
    juce::ignoreUnused (slot);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::requestReferenceRecovery()
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->requestRecovery();
   #else
    return false;
   #endif
}

bool KirinHyphaProcessorBase::startReferenceBlind (double aIntegratedLoudness,
                                                   double aMaximumTruePeakDbtp)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->startBlind (
            aIntegratedLoudness, aMaximumTruePeakDbtp);
   #else
    juce::ignoreUnused (aIntegratedLoudness, aMaximumTruePeakDbtp);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceBlindStimulus (int stimulus)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->selectBlindStimulus (stimulus);
   #else
    juce::ignoreUnused (stimulus);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::approveReferenceBlindLowerA (
    double aIntegratedLoudness, double aMaximumTruePeakDbtp)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->approveBlindLowerAAndStart (
            aIntegratedLoudness, aMaximumTruePeakDbtp);
   #else
    juce::ignoreUnused (aIntegratedLoudness, aMaximumTruePeakDbtp);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::answerReferenceBlind (int stimulus)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->answerBlind (stimulus);
   #else
    juce::ignoreUnused (stimulus);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::revealReferenceBlind()
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->revealBlind();
   #else
    return false;
   #endif
}

void KirinHyphaProcessorBase::endReferenceBlind()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr)
        referenceAuditionController->endBlind();
   #endif
}

#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaProcessorBase::createReferenceAuditionController()
{
    referenceAuditionController = std::make_unique<hypha::reference_audition::ReferenceComparisonController> (
        hypha::reference_audition::RuntimeV2Repository::transportRoot(), [this] (bool active)
        {
            const juce::ScopedLock gateLock (handleLock);
            return hyphaHandle != nullptr
                && kirin_hypha_set_reference_audition_active (hyphaHandle, active);
        }, [this](bool active) {
            const juce::ScopedLock lock(handleLock);
            const bool accepted=hyphaHandle && kirin_hypha_set_reference_capture_active(hyphaHandle,active);
            if(accepted && !active) captureStateNotification.changed();
            return accepted;
        }, [this](bool active) {
            const juce::ScopedLock lock(handleLock);
            return hyphaHandle && kirin_hypha_set_version_blind_capture_exclusion(hyphaHandle,active);
        }, [this] { captureStateNotification.changed(); });
}
#endif


void KirinHyphaProcessorBase::configureReferenceAudition()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (role != Role::Post) return;
    if (! stereoWorkflowsSupported())
    {
        referenceAuditionController.reset();
        return;
    }
    if (referenceAuditionController == nullptr) createReferenceAuditionController();
    hypha::reference_audition::RuntimeIdentity identity;
    identity.runtimeInstanceId = referenceRuntimeId;
    identity.library = true;
    referenceAuditionController->setAnalysisOwner(kirin_hypha_reference_analysis_owner(hyphaHandle));
    referenceAuditionController->configure (identity, preparedFormat.sampleRate, static_cast<int> (preparedFormat.channelRoles.size()));
   #endif
}
