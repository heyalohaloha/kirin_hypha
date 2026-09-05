#include "PluginProcessor.h"

#if KIRIN_HYPHA_GUIDE_TRANSPORT

hypha::pre_display::DisplaySnapshot KirinHyphaProcessorBase::preDisplaySnapshot() const
{
    return licenseIsOs() && preDisplayController != nullptr
        ? preDisplayController->displaySnapshot()
        : hypha::pre_display::DisplaySnapshot {};
}

hypha::pre_display::GuidePresentationSnapshot
KirinHyphaProcessorBase::guidePresentationSnapshot() const
{
    return licenseIsOs() && preDisplayController != nullptr
        ? preDisplayController->guidePresentationSnapshot()
        : hypha::pre_display::GuidePresentationSnapshot {};
}

hypha::pre_display::ConnectionRequest KirinHyphaProcessorBase::pendingPreDisplayConnection() const
{
    return licenseIsOs() && preDisplayController != nullptr
        ? preDisplayController->pendingConnection()
        : hypha::pre_display::ConnectionRequest {};
}

bool KirinHyphaProcessorBase::acceptPreDisplayConnection()
{
    refreshLicenseForUserAction();
    if (! licenseIsOs() || preDisplayController == nullptr
        || ! preDisplayController->acceptPendingConnection())
        return false;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr)
    {
        const auto work = preDisplayController->connectedWorkReference();
        hypha::reference_audition::RuntimeIdentity identity;
        identity.runtimeInstanceId = work.runtimeInstanceId;
        identity.workId = work.workId;
        referenceAuditionController->configure (
            std::move (identity), preparedSampleRate, preparedInputChannels);
    }
   #endif
    return true;
}

hypha::pre_display::WorkReference KirinHyphaProcessorBase::connectedWorkReference() const
{
    return licenseIsOs() && preDisplayController != nullptr
        ? preDisplayController->connectedWorkReference()
        : hypha::pre_display::WorkReference {};
}

juce::String KirinHyphaProcessorBase::connectedWorkTitle() const
{
    return licenseIsOs() && preDisplayController != nullptr
        ? preDisplayController->connectedWorkTitle() : juce::String {};
}

hypha::capture::WorkAttachmentSubmit KirinHyphaProcessorBase::attachCaptureToWork (
    const hypha::pre_display::WorkReference& expectedWork,
    juce::MemoryBlock pngBytes,
    hypha::capture::WorkAttachmentDescriptor descriptor)
{
    refreshLicenseForUserAction();
    if (! licenseIsOs() || preDisplayController == nullptr
        || captureWorkAttachmentController == nullptr
        || ! connectedWorkReference().sameAuthority (expectedWork))
        return hypha::capture::WorkAttachmentSubmit::invalidReference;
    return captureWorkAttachmentController->submit (
        expectedWork, std::move (pngBytes), std::move (descriptor));
}

hypha::capture::WorkAttachmentResult
KirinHyphaProcessorBase::takeCaptureWorkAttachmentResult()
{
    return captureWorkAttachmentController != nullptr
        ? captureWorkAttachmentController->takeResult()
        : hypha::capture::WorkAttachmentResult {};
}

hypha::reference_audition::Snapshot KirinHyphaProcessorBase::referenceAuditionSnapshot() const
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
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
            referenceAuditionController->selectA();
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
            referenceAuditionController->endBlind();
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
            referenceAuditionController->endBlind();
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
    return licenseIsOs() && referenceAuditionController != nullptr
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
    return licenseIsOs() && referenceAuditionController != nullptr
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
            referenceAuditionController->endBlind();
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

#endif
