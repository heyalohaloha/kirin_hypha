#include "PluginProcessor.h"


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

bool KirinHyphaProcessorBase::selectReferenceB (double aIntegratedLoudness,
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
        && referenceAuditionController->selectB (
            aIntegratedLoudness, aMaximumTruePeakDbtp);
   #else
    juce::ignoreUnused (aIntegratedLoudness, aMaximumTruePeakDbtp);
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

bool KirinHyphaProcessorBase::approveReferenceSampleRateConversion()
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->approveSampleRateConversion();
   #else
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
    referenceAuditionController = std::make_unique<hypha::reference_audition::RuntimeV2Controller> (
        hypha::reference_audition::RuntimeV2Repository::transportRoot(), [this] (bool active)
        {
            const juce::ScopedLock gateLock (handleLock);
            return hyphaHandle != nullptr
                && kirin_hypha_set_reference_audition_active (hyphaHandle, active);
        });
}
#endif


void KirinHyphaProcessorBase::configureReferenceAudition()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (role != Role::Post) return;
    if (referenceAuditionController == nullptr) createReferenceAuditionController();
    hypha::reference_audition::RuntimeIdentity identity;
    identity.runtimeInstanceId = referenceRuntimeId;
    identity.library = true;
    referenceAuditionController->configure (identity, preparedSampleRate, preparedInputChannels);
   #endif
}
