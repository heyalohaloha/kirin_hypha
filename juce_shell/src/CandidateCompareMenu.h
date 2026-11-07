#include "HyphaPairLabels.h"
// Included once by PluginEditorLiveCompare.cpp. A/B uses the ordinary LIVE controls thereafter.
#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaEditor::showCandidateCompareMenu()
{
    if (processorRef.isRecording() || processorRef.keepPhase() != KIRIN_KEEP_PHASE_IDLE)
    { showToast ("A/B: finish Record / Keep first"); return; }
    liveCompareStartAfterReturnCandidate.clear();
    liveCompareStartAfterReturnUntil = 0.0;
    const auto original = processorRef.pairedPreInstanceId();
    if (original.isEmpty()) { showToast ("Choose the PRE first"); showCandidateMenu(); return; }
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("A/B: PRE2");
    juce::Array<KirinHyphaProcessorBase::PreCandidate> choices;
    const auto candidates = processorRef.enumeratePreCandidatesForMenu();
    const auto claims = processorRef.enumeratePostPairClaims();
    for (const auto& candidate : candidates)
        if (candidate.instanceId != original)
        {
            choices.add (candidate);
            // Marked, not refused: the chain is the listener's to arrange.
            const auto claimed = hypha::pair_labels::claimedByOtherPost (candidate, processorRef.instanceId(), claims);
            menu.addItem (choices.size(), (claimed ? "In use by another POST: " : "")
                + hypha::pair_labels::menuLabel (candidates, candidate));
        }
    if (choices.isEmpty()) { showToast ("A/B: add PRE2 between A and B"); return; }
    const auto ticket = processorRef.restoreOrPairTicket();
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&observatoryView.candidateAnchor())
        .withDeletionCheck (*this).withMinimumWidth (260).withMaximumNumColumns (1),
        [safe, choices, ticket] (int selected)
        {
            if (safe == nullptr || selected < 1 || selected > choices.size()) return;
            safe->confirmCandidateCompare (choices[selected - 1].instanceId, ticket);
        });
}

void KirinHyphaEditor::confirmCandidateCompare (const juce::String& pre2, std::uint64_t ticket)
{
    if (ticket != processorRef.restoreOrPairTicket()) return;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader (juce::String::fromUTF8 (u8"PRE1 → A → PRE2 → B → POST"));
    menu.addSectionHeader ("One track. Hardware in A or B.");
    menu.addSectionHeader ("A different order changes the sound during A/B.");
    menu.addItem (1, "START A/B");
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&observatoryView.candidateAnchor())
        .withDeletionCheck (*this).withMinimumWidth (280).withMaximumNumColumns (1),
        [safe, pre2, ticket] (int selected)
        {
            if (safe == nullptr || selected != 1 || ticket != safe->processorRef.restoreOrPairTicket()) return;
            safe->startCandidateConfirmed (pre2, ticket);
        });
}
void KirinHyphaEditor::startCandidateConfirmed (const juce::String& pre2, std::uint64_t ticket, bool waitForReturn)
{
    const auto result = processorRef.startCandidateCompare (pre2, true, ticket);
    refreshLiveCompare();
    if (waitForReturn && (result == hypha::live_compare::StartResult::returnRequired
        || result == hypha::live_compare::StartResult::returnPending))
    {
        observatoryView.onLiveCompareReturn();
        liveCompareStartAfterReturnUntil = nowSecs() + 10.0;
        liveCompareStartAfterReturnKey = waitingPairKey();
        liveCompareStartAfterReturnTicket = ticket;
        liveCompareStartAfterReturnBlind = false;
        liveCompareStartAfterReturnCandidate = pre2;
        showToast ("Returning to normal, then A/B");
    }
    else if (result != hypha::live_compare::StartResult::started)
        showToast (result == hypha::live_compare::StartResult::comparisonBusy
            ? "A/B: Record / Keep or another comparison is running"
            : result == hypha::live_compare::StartResult::notReady ? "A/B could not start" : hypha::live_compare_ui::startFailure (result));
    else if (processorRef.candidateTrackMismatch()) showToast ("A/B: track names differ");
}
#endif
