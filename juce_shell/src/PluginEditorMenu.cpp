#include "PluginEditor.h"

namespace
{
    namespace ui = hypha::ui_contract;
    juce::String allKeepMenuLabel (int nReady)
    {
        return juce::String ("All Keep: ") + juce::String (nReady) + " ready POST"
             + (nReady == 1 ? "" : "s");
    }

    bool claimedByOtherPost (const KirinHyphaProcessorBase::PreCandidate& candidate,
                             const juce::String& ownInstanceId,
                             const juce::Array<KirinHyphaProcessorBase::PostPairClaim>& claims)
    {
        for (const auto& c : claims)
            if (c.instanceId != ownInstanceId
                && ((c.hasPairedPreInstanceId && c.pairedPreInstanceId == candidate.instanceId)
                    || (! c.hasPairedPreInstanceId && c.hasPairPreName && candidate.hasName
                        && candidate.name.isNotEmpty() && c.pairPreName == candidate.name)))
                return true;
        return false;
    }

    juce::String resolvedOwnPreInstanceId (
        const juce::String& ownInstanceId,
        const juce::String& latchedPreInstanceId,
        const juce::Array<KirinHyphaProcessorBase::PostPairClaim>& claims)
    {
        for (const auto& claim : claims)
            if (claim.instanceId == ownInstanceId && claim.hasPairedPreInstanceId)
                return claim.pairedPreInstanceId;
        return latchedPreInstanceId;
    }
}

KirinHyphaEditor::PairMenuLookAndFeel& KirinHyphaEditor::pairMenuLookAndFeel()
{
    static PairMenuLookAndFeel lookAndFeel;
    return lookAndFeel;
}

void KirinHyphaEditor::showCandidateMenu()
{
    processorRef.refreshLicenseForUserAction();
    // B-102: built on click; every live PRE remains independently selectable by exact identity.
    // Keep operations stay visible while their license/pair prerequisites control availability.
    const bool rec = processorRef.isRecording();
    const bool keepActive = rec
        || processorRef.keepPhase() != (int) KIRIN_KEEP_PHASE_IDLE;
    const bool playing = processorRef.isPlaying(); // W-280: pair change locked during playback
    // B-115: lock only when playing AND live (processBlock running). A frozen `playing` with a
    // stalled heartbeat does not lock (false-release prevention; signal_state is silence-conflated).
    const bool pairLocked = playing && processorRef.heartbeatLive();
    const auto cands = processorRef.enumeratePreCandidates();
    const auto claims = processorRef.enumeratePostPairClaims();
    const juce::String ownInstanceId = processorRef.instanceId();

    juce::StringArray labels;
    juce::Array<bool> labelEnabled;
    juce::Array<bool> labelChecked;
    const juce::String currentPreInstanceId = resolvedOwnPreInstanceId (
        ownInstanceId, processorRef.pairedPreInstanceId(), claims);
    for (const auto& c : cands)
    {
        const bool keepReady = currentPreInstanceId.isNotEmpty()
                                 && c.instanceId == currentPreInstanceId;
        const bool inUse = claimedByOtherPost (c, ownInstanceId, claims);
        int sameNameCount = 0;
        if (c.hasName && c.name.isNotEmpty())
            for (const auto& other : cands)
                if (other.hasName && other.name == c.name)
                    ++sameNameCount;
        const juce::String shown = c.hasName && c.name.isNotEmpty()
                                     ? c.name + (sameNameCount > 1
                                                     ? " · " + c.instanceId.substring (0, 8)
                                                     : juce::String())
                                     : c.instanceId.substring (0, 8);
        labels.add ((inUse ? "In use: " : (keepReady ? "Keep ready: " : "Can Keep: ")) + shown);
        labelEnabled.add (! inUse);
        labelChecked.add (keepReady && ! inUse);
    }

    // egui parity: "N ready" = pair-set POST instances (keepReadyCount), NOT the PRE candidate
    // count — the All Keep broadcast acts on POSTs (hypha_post editor.rs:938-944). Candidate rows
    // below are exact PRE rows, matching egui's separate pre_candidates source.
    const int nReady = processorRef.keepReadyCount();
    const bool osOwned = processorRef.licenseIsOs();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Display");
    menu.addItem (10, "Show hover help", true,
                  hypha::HoverHelpPreference::shared().isEnabled());
    menu.addSeparator();
    const bool pairSelected = processorRef.pairStatus() != KIRIN_PAIR_STATUS_UNPAIRED;
    if (! keepActive)
        menu.addItem (4, "Keep selected pair", osOwned && pairSelected);
    if (keepActive)
        menu.addItem (5, "Stop selected pair");
    if (! keepActive)
        menu.addItem (1, osOwned ? allKeepMenuLabel (nReady) : "All Keep: Kirin OS required",
                      osOwned && nReady >= 1);
    if (keepActive)
        menu.addItem (2, "All Stop: active POSTs");
    if (menu.getNumItems() > 0)
        menu.addSeparator();
    menu.addSectionHeader ("Pair choices (not Keep targets)");
    if (cands.isEmpty())
        menu.addItem (3, "No pair choices", false, false); // disabled (R-26: silent when nothing)
    else
        for (int i = 0; i < labels.size(); ++i)
            menu.addItem (100 + i, labels[i], ! pairLocked && labelEnabled[i], labelChecked[i]);

    const auto options = juce::PopupMenu::Options()
                             .withTargetComponent (&pairDropdown)
                             .withDeletionCheck (*this)
                             .withMinimumWidth (ui::pairMenuMinimumWidth)
                             .withMaximumNumColumns (ui::pairMenuMaximumColumns)
                             .withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);
    menu.showMenuAsync (options, [safeThis, candidates = cands] (int result)
    {
        if (safeThis != nullptr)
            safeThis->handleCandidateMenu (result, candidates);
    });
}

void KirinHyphaEditor::handleCandidateMenu (
    int result, const juce::Array<KirinHyphaProcessorBase::PreCandidate>& candidates)
{
    if (result == 1)
    {
        if (! processorRef.keepAll())
        {
            const juce::String notice = processorRef.drainKeepActionNotice();
            if (notice.isNotEmpty()) { showToast (notice); return; }
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            showToast (processorRef.licenseIsOs() ? "No PRE Paired" : "Record requires Kirin OS license");
        }
    }
    else if (result == 2)
        processorRef.stopAll();
    else if (result == 4)
    {
        if (! processorRef.keepPair())
        {
            const juce::String notice = processorRef.drainKeepActionNotice();
            if (notice.isNotEmpty()) { showToast (notice); return; }
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            showToast (processorRef.licenseIsOs()
                           ? "No PRE Paired" : "Record requires Kirin OS license");
        }
    }
    else if (result == 5)
        processorRef.stopPair();
    else if (result == 10)
    {
        auto& preference = hypha::HoverHelpPreference::shared();
        const bool enabled = ! preference.isEnabled();
        const bool persisted = preference.setEnabled (enabled);
        if (! enabled)
            tooltip.hideTip();
        if (! persisted)
            showToast ("Hover help changed for this session only");
    }
    else if (result >= 100)
    {
        const int idx = result - 100;
        if (idx >= 0 && idx < candidates.size())
        {
            const auto candidate = candidates.getReference (idx);
            const juce::String name = candidate.hasName ? candidate.name : juce::String();
            if (processorRef.setPairCandidate (candidate.instanceId, name))
            {
                pairedPreExplicitlyBypassed = false;
               #if ! KIRIN_HYPHA_PRE_DISPLAY
                spectrumView.clearSnapshot();
                perceptualView.clearSnapshot();
               #endif
                nameField.setModelName (name);
                nameField.setFallback (name.isEmpty() ? candidate.instanceId.substring (0, 8)
                                                      : juce::String ("___"));
            }
            else
            {
                const juce::String notice = processorRef.drainKeepActionNotice();
                showToast (notice.isNotEmpty() ? notice : juce::String ("PRE no longer available"));
            }
        }
    }
}
