#include "PluginProcessor.h"

#if KIRIN_HYPHA_GUIDE_TRANSPORT

hypha::pre_display::DisplaySnapshot KirinHyphaProcessorBase::preDisplaySnapshot() const
{
    return preDisplayController != nullptr
        ? preDisplayController->displaySnapshot()
        : hypha::pre_display::DisplaySnapshot {};
}

hypha::pre_display::GuidePresentationSnapshot
KirinHyphaProcessorBase::guidePresentationSnapshot() const
{
    return preDisplayController != nullptr
        ? preDisplayController->guidePresentationSnapshot()
        : hypha::pre_display::GuidePresentationSnapshot {};
}

hypha::pre_display::ConnectionRequest KirinHyphaProcessorBase::pendingPreDisplayConnection() const
{
    return preDisplayController != nullptr
        ? preDisplayController->pendingConnection()
        : hypha::pre_display::ConnectionRequest {};
}

bool KirinHyphaProcessorBase::acceptPreDisplayConnection()
{
    if (preDisplayController == nullptr || ! preDisplayController->acceptPendingConnection())
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
    return preDisplayController != nullptr
        ? preDisplayController->connectedWorkReference()
        : hypha::pre_display::WorkReference {};
}

juce::String KirinHyphaProcessorBase::connectedWorkTitle() const
{
    return preDisplayController != nullptr
        ? preDisplayController->connectedWorkTitle() : juce::String {};
}

hypha::capture::WorkAttachmentSubmit KirinHyphaProcessorBase::attachCaptureToWork (
    const hypha::pre_display::WorkReference& expectedWork,
    juce::MemoryBlock pngBytes,
    hypha::capture::WorkAttachmentDescriptor descriptor)
{
    if (preDisplayController == nullptr || captureWorkAttachmentController == nullptr
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
   #if ! KIRIN_HYPHA_PRE_DISPLAY
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

bool KirinHyphaProcessorBase::startReferenceBlind (double aIntegratedLoudness,
                                                   double aMaximumTruePeakDbtp)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
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
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController != nullptr
        && referenceAuditionController->selectBlindStimulus (stimulus);
   #else
    juce::ignoreUnused (stimulus);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::revealReferenceBlind()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
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
    referenceAuditionController = std::make_unique<hypha::reference_audition::Controller> (
        hypha::reference_audition::Repository::transportRoot(), [this] (bool active)
        {
            const juce::ScopedLock gateLock (handleLock);
            return hyphaHandle != nullptr
                && kirin_hypha_set_reference_audition_active (hyphaHandle, active);
        });
}
#endif

#endif
