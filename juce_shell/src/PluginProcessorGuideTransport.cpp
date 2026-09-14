#include "PluginProcessor.h"
#include "HyphaPluginFormat.h"

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

void KirinHyphaProcessorBase::configureWorkTransports()
{
    if (preDisplayController == nullptr)
        preDisplayController = std::make_unique<hypha::pre_display::Controller> (preDisplayClock);
    if (role == Role::Post && captureWorkAttachmentController == nullptr)
        captureWorkAttachmentController =
            std::make_unique<hypha::capture::WorkAttachmentController>();
    hypha::pre_display::RuntimeIdentity displayIdentity;
    displayIdentity.role = role == Role::Post ? hypha::pre_display::GuideTargetRole::post
                                              : hypha::pre_display::GuideTargetRole::pre;
    displayIdentity.instanceId = persistInstanceId;
    displayIdentity.projectUuid = persistProjectUuid;
    displayIdentity.dawSessionUuid = persistDawSessionUuid;
    displayIdentity.name = persistName;
    displayIdentity.pluginVersion = JucePlugin_VersionString;
    displayIdentity.pluginFormat = hypha::plugin_format::name (wrapperType);
       #if JUCE_WINDOWS
    displayIdentity.platform = "windows";
       #else
    displayIdentity.platform = "macos";
       #endif
       #if JUCE_ARM
    displayIdentity.architecture = "arm64";
       #else
    displayIdentity.architecture = "x86_64";
       #endif
    preDisplayController->configureAndStart (std::move (displayIdentity));
}

#else
void KirinHyphaProcessorBase::configureWorkTransports() {}
#endif
