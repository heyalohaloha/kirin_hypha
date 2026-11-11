#include "PluginEditor.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaReferenceAuditionRecoveryText.h"
#include "HyphaLiveStartAfterReturn.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY
using hypha::live_compare::BlindStage;
using hypha::live_compare::StartResult;

void KirinHyphaEditor::configureLiveBlind()
{
    observatoryView.onLocalBlind = [this] { openLiveBlind(); };
    scaleRoot.addChildComponent (liveBlindView);
    liveBlindView.onSelect = [this] (int source)
    {
        processorRef.selectLiveBlind (source);
        refreshLiveBlind();
    };
    liveBlindView.onEnd = [this]
    {
        processorRef.finishLiveCompare();
        refreshLiveBlind();
    };
    liveBlindView.onAlignOff = [this]
    {
        processorRef.setLiveCompareAutoAlign (false);
        refreshLiveBlind();
    };
    liveBlindView.onApprove = [this]
    {
        processorRef.approveLiveBlindMatch (liveBlindView.state().generation);
        refreshLiveBlind();
    };
    const auto stage = processorRef.liveBlindStatus().stage;
    liveBlindOpen = stage != BlindStage::idle && stage != BlindStage::finishing;
    liveBlindView.onReveal = [this]
    {
        processorRef.revealLiveBlind();
        refreshLiveBlind();
    };
}

void KirinHyphaEditor::openLiveBlind (std::optional<std::uint64_t> waitedTicket)
{
    if (localBlindOpen || liveBlindOpen) return;
    liveCompareStartAfterReturnCandidate.clear();
    const auto result = processorRef.beginLiveBlind (waitedTicket);
    if (result != StartResult::started)
    {
        refreshLiveCompare(); // acknowledge older transitions before this direct refusal
        if (waitedTicket && processorRef.restoreOrPairTicket() != *waitedTicket)
            return;  // restored or re-paired since the press: the press is dropped, not refused
        // INV-LC14: the same RETURN as LISTEN, then BLIND on this press.
        if (! waitedTicket && hypha::live_compare_ui::onStartPressed (result) == hypha::live_compare_ui::StartAfterReturn::returnFirst)
        {
            observatoryView.onLiveCompareReturn();
            liveCompareStartAfterReturnUntil = nowSecs() + 10.0;
            liveCompareStartAfterReturnKey = waitingPairKey();
            liveCompareStartAfterReturnTicket = processorRef.restoreOrPairTicket();
            liveCompareStartAfterReturnBlind = true;
            showToast ("Returning POST to normal, then BLIND");
            return;
        }
        if (hypha::live_compare_ui::startNeedsPreChoice (result))
        {
            if (result == StartResult::preUnavailable) showToast (hypha::live_compare_ui::preUnavailableNotice());
            showCandidateMenu();
        }
        else showToast (result == StartResult::contentHeld ? "Timing changed: check chain delay (POST)"
                      : result == StartResult::returnPending ? "Ended; normal level returns with audio"
                      : result == StartResult::returnRequired ? "Press RETURN first"
                      : result == StartResult::comparisonBusy ? "End the current comparison first"
                                                            : "BLIND COMPARE COULD NOT START");
        return;
    }
    liveCompareAuto = {}; // freeze the last explicit MATCH; AUTO never enters Blind
    liveBlindOpen = true;
    juce::PopupMenu::dismissAllActiveMenus();
    // Isolate every measurement, tooltip and accessibility sibling BEFORE publishing an anonymous source.
    layoutLocalBlindProduct();
    refreshLiveBlind();
    if (liveBlindView.isShowing()) liveBlindView.grabKeyboardFocus();
}

void KirinHyphaEditor::refreshLiveBlind()
{
    if (!liveBlindOpen)
    {
        const auto stage = processorRef.liveBlindStatus().stage;
        if (stage == BlindStage::idle || stage == BlindStage::finishing) return;
        liveBlindOpen = true;
        layoutLocalBlindProduct();
    }
    monitorLiveCompareOffset (processorRef.liveCompareStatus(), nowSecs());
    processorRef.serviceLiveBlind();
    const auto status = processorRef.liveCompareStatus();
    const auto blind = processorRef.liveBlindStatus();
    // END is accepted even without callbacks. Only the non-modal audio-return notice remains.
    if (blind.stage == BlindStage::idle || status.finishing)
    {
        liveBlindOpen = false;
        liveCompareActiveSeen = false;
        layoutLocalBlindProduct();
        resized();
        refreshObservatory();
        if (processorRef.takeCandidateBlindReturned()) showToast ("BLIND did not start; A/B continues");
        return;
    }
    liveBlindView.setState (blind, processorRef.isPlaying(), status.postActual,
                           processorRef.liveContentTimingObservation().verifySamples);
}

void KirinHyphaEditor::addLiveCompareMenu (juce::PopupMenu& menu, bool keepActive)
{
    const auto live = processorRef.liveCompareStatus();
    if (! processorRef.stereoWorkflowsSupported() || ! processorRef.liveCompareSupported())
    {
        // No comparison on this POST (INV-LC9): MENU still says why.
        menu.addSectionHeader ("PRE / POST");
        hypha::unavailable::addItem (menu, 40, "LISTEN", false, liveCompareEntryRefusal (false));
        return;
    }
    menu.addSectionHeader (processorRef.candidateCompareActive() ? "A / B" : "PRE / POST");
    // A/B opens on the same terms as LISTEN; the reason line under LISTEN says why both are greyed.
    if (! live.active) menu.addItem (45, "A/B", hypha::live_compare_ui::entryOffered (processorRef.liveCompareAdmission (false)));
    const auto recovery = hypha::live_compare_ui::candidateNamedPresentation (live,
        hypha::live_compare_ui::currentListenPresentation (live, processorRef.liveCompareAdmission (false), processorRef.referenceOutputSlot()),
        processorRef.candidateCompareActive(), processorRef.candidateHostInactive(), processorRef.candidateBypassed());
    if (*recovery.instruction != 0) menu.addSectionHeader (recovery.instruction);
    // A greyed entry carries its reason, unless the heading above already says it.
    const auto reasonFor = [&recovery] (const char* text) { return juce::String (text) == recovery.instruction ? "" : text; };
    if (! live.active)
        hypha::unavailable::addItem (menu, 40, "LISTEN", hypha::live_compare_ui::entryOffered (processorRef.liveCompareAdmission (false)),
                                     reasonFor (liveCompareEntryRefusal (false)));
    else
    {
        menu.addItem (41, "MATCH", ! live.finishing);
        menu.addItem (42, processorRef.candidateCompareActive() ? "A" : "PRE", ! live.finishing);
        menu.addItem (43, processorRef.candidateCompareActive() ? "B" : "POST", ! live.finishing);
    }
    if (observatoryView.localBlindEntryAvailable())
        hypha::unavailable::addItem (menu, 23, "BLIND",
            ! keepActive && hypha::live_compare_ui::entryOffered (processorRef.liveCompareAdmission (true)),
            reasonFor (keepActive ? "Finish Keep / Record before PRE / POST Blind" : liveCompareEntryRefusal (true)));
    menu.addItem (44, processorRef.candidateCompareActive() ? "Align A automatically" : "Align PRE automatically", true, processorRef.liveCompareAutoAlign());
}

bool KirinHyphaEditor::handleLiveCompareMenu (int result)
{
    // 1.1.52 shows no PIN 4 S entry (Exact 4 S); its internals leave in a later release.
    if (result == 45) { showCandidateCompareMenu(); return true; }
    if (result < 40 || result > 44) return false;
    if (result == 44) processorRef.setLiveCompareAutoAlign (! processorRef.liveCompareAutoAlign());
    if (result == 40) requestListenStart();
    if (result == 41) requestListenMatch();
    if (result == 42 || result == 43) requestListenSelect (result == 42);
    return true;
}
#endif
