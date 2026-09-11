#include "PluginEditor.h"

void KirinHyphaEditor::refreshAppearance()
{
    auto& service = hypha::appearance::Service::shared();
    const bool shouldBeRegistered = isShowing();
    if (shouldBeRegistered != appearanceVisibleRegistered)
    {
        appearanceVisibleRegistered = shouldBeRegistered;
        if (appearanceVisibleRegistered)
            service.editorBecameVisible();
        else
            service.editorBecameHidden();
    }
    if (appearanceVisibleRegistered)
        service.pulse();
    const auto next = service.snapshot();
    const bool blindBusy = informationBlockedByBlind();
    const bool initialLink = ! appearanceSnapshot.activationSeen && next.activationSeen;
    const bool initialLinkBusy = processorRef.isPlaying() || processorRef.isRecording()
        || noteDialog != nullptr || captureChooser != nullptr
        || juce::Component::getNumCurrentlyModalComponents() > 0;
    if (next.enabled != observatoryView.jungleAppearanceEnabled())
    {
        if (blindBusy || ((initialLink || appearanceApplyPending) && initialLinkBusy))
            appearanceApplyPending = true;
        else
        {
            observatoryView.setJungleAppearance (next.enabled);
            appearanceApplyPending = false;
        }
    }
    else
        appearanceApplyPending = false;
    appearanceSnapshot = next;

    if (appearanceActionAwaited != 0)
    {
        const auto action = service.latestUserAction();
        if (action.id > appearanceActionAwaited)
            appearanceActionAwaited = 0;
        else if (action.id == appearanceActionAwaited
            && action.state != hypha::appearance::UserActionState::pending)
        {
            appearanceActionAwaited = 0;
            if (action.state == hypha::appearance::UserActionState::failed)
                showToast ("Jungle Mode changed for this session only");
            else if (action.state == hypha::appearance::UserActionState::unsupported)
                showToast ("Jungle Mode could not be saved by this Hypha version");
        }
    }
}

void KirinHyphaEditor::requestJungleChoice (bool enabled)
{
    if (informationBlockedByBlind())
    {
        showToast ("Available after Blind Compare");
        return;
    }
    if (! appearanceSnapshot.activationSeen)
        return;
    observatoryView.setJungleAppearance (enabled);
    appearanceApplyPending = false;
    appearanceSnapshot.enabled = enabled;
    const auto receipt = hypha::appearance::Service::shared().setChoice (
        enabled ? hypha::appearance::Choice::on : hypha::appearance::Choice::off);
    appearanceActionAwaited = receipt.id;
    if (receipt.state == hypha::appearance::UserActionState::unsupported)
    {
        appearanceActionAwaited = 0;
        showToast ("Jungle Mode could not be saved by this Hypha version");
    }
}

void KirinHyphaEditor::releaseAppearanceVisibility()
{
    if (! appearanceVisibleRegistered)
        return;
    appearanceVisibleRegistered = false;
    hypha::appearance::Service::shared().editorBecameHidden();
}
