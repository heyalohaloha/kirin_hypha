#include "PluginEditor.h"

namespace
{
    namespace ui = hypha::ui_contract;
    juce::String allKeepMenuLabel (int nReady)
    {
        return juce::String ("All Keep: ") + juce::String (nReady) + " ready POST"
             + (nReady == 1 ? "" : "s");
    }

}

void KirinHyphaEditor::showOperationsMenu()
{
    processorRef.refreshLicenseForUserAction();
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.operationsMenuAnchor())
        .withDeletionCheck (*this).withMinimumWidth (juce::jlimit (300, 420, getWidth()))
        .withMaximumNumColumns (1).withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    operationsMenu().showMenuAsync (options, [safe] (int result)
    { if (safe != nullptr) safe->handleOperationsMenu (result); });
}

// MENU as it stands now, every level (built apart from showing it, so a test reads what is offered).
juce::PopupMenu KirinHyphaEditor::operationsMenu()
{
    const bool recording = processorRef.isRecording();
    const bool keepActive = recording || keepPreWaitSince > 0.0  // a Keep waiting for PRE offers Stop
        || processorRef.keepPhase() != (int) KIRIN_KEEP_PHASE_IDLE;
    const bool osOwned = processorRef.licenseIsOs();
    const bool stereoWorkflows = processorRef.stereoWorkflowsSupported();
    const int nReady = processorRef.keepReadyCount();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    if (isPost && stereoWorkflows)
    {
        menu.addSectionHeader ("Keep");
        if (keepActive)
        {
            menu.addItem (5, "Stop selected pair");
            menu.addItem (2, "All Stop: active POSTs");
        }
        else
        {
            menu.addItem (4, "Keep selected pair", osOwned);
            menu.addItem (1, osOwned ? allKeepMenuLabel (nReady)
                                     : "All Keep: Kirin OS required",
                          osOwned && nReady >= 1);
        }
        menu.addSeparator();
    }
    else if (isPost)
    {
        menu.addSectionHeader ("Keep");
        menu.addItem (3, "Record / Keep: mono / stereo only", false, false);
        menu.addSeparator();
    }
    menu.addSectionHeader ("Measurement");
    menu.addItem (20, "Reset Meter Session");
    if (isPost)
    {
        if (recording && stereoWorkflows)
            menu.addItem (22, "Add NOTE at current position", osOwned);
        if (observatoryDomain != hypha::observatory::Domain::reference)
            menu.addItem (21, "Save measurement image");
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        addLiveCompareMenu (menu, keepActive);
       #endif
    }
    menu.addSeparator();
    menu.addSectionHeader ("Display");
    if (stereoWorkflows)
    {
        menu.addItem (11, "Show Hybrid VU while recording", true,
                      processorRef.hybridVuOnRecordPreference());
        if (observatoryView.hybridVuShownByRecording())
            menu.addItem (12, "Show selected view for this recording");
    }
    menu.addItem (10, "Show hover help", true,
                  hypha::HoverHelpPreference::shared().isEnabled());
    menu.addItem (24, "KEYS");
    addLanguageMenu (menu);
    if (appearanceSnapshot.activationSeen)
        menu.addItem (jungleModeMenuAction, "Jungle Mode", true,
                      observatoryView.jungleAppearanceEnabled());
    return menu;
}

void KirinHyphaEditor::handleOperationsMenu (int result)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (liveBlindOpen || localBlindOpen) return;
    if (handleLiveCompareMenu (result)) return;
   #endif
    if (handleLanguageMenu (result)) return;
    if (result == 20)
    {
        if (observatoryView.onReset) observatoryView.onReset();
    }
    else if (result == 21)
    {
        if (observatoryView.onCapture) observatoryView.onCapture();
    }
    else if (result == 22)
    {
        if (observatoryView.onNote) observatoryView.onNote();
    }
    else if (result == 23)
    {
        if (observatoryView.onLocalBlind) observatoryView.onLocalBlind();
    }
    else if (result == 24)
        openKeysList();
    else if (result == 11 || result == 12)
        handleInformationMenu (result);
    else
        handleCandidateMenu (result, {});
}
