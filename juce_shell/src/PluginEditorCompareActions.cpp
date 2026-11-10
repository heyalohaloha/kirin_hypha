#include "PluginEditor.h"
#include "HyphaLiveCompareActionResult.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaLiveStartAfterReturn.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY
using hypha::live_compare::StartResult;
const char* hypha::live_compare_ui::startFailure (StartResult result) noexcept
{
    switch (result)
    {
        case StartResult::started:
        case StartResult::notPost:           return "";
        case StartResult::contentHeld:       return "Timing changed: check chain delay (POST)";
        case StartResult::comparisonBusy:    return "End the current comparison first";
        case StartResult::returnPending:     return "Ended; normal level returns with audio";
        case StartResult::returnRequired:    return "Press RETURN first";
        case StartResult::notReady:          return "LISTEN could not start";
        case StartResult::noPair:            return "Choose the PRE first";
        case StartResult::unsupportedLayout: return "Mono / stereo only";
        case StartResult::preUnavailable:    return hypha::live_compare_ui::preUnavailableNotice();
        case StartResult::preMultiMono:      return "PRE is multi-mono: insert it as stereo";
    }
    return "";
}

// Why LISTEN and A/B (blind: BLIND) cannot open now, or nothing when they can. An unsupported POST
// offers no footer entry (INV-LC9); MENU and the keys still say why.
const char* KirinHyphaEditor::liveCompareEntryRefusal (bool blind) const
{
    if (! processorRef.stereoWorkflowsSupported()) return "Mono / stereo only";
    if (processorRef.aaxMultiMonoMember()) return "POST is multi-mono: insert it as stereo";
    const auto admission = processorRef.liveCompareAdmission (blind);
    if (hypha::live_compare_ui::entryOffered (admission)) return "";
    using hypha::output_owner::Activity;
    const char* owner = hypha::output_owner::refusalText (
        processorRef.outputDecision (blind ? Activity::liveBlind : Activity::liveCompare).reason);
    return *owner != 0 ? owner : hypha::live_compare_ui::startFailure (admission);
}

void KirinHyphaEditor::requestListenStart ()
{
    liveCompareStartAfterReturnCandidate.clear();
    afterTail = {};  // LISTEN is the newer choice: a V, C, B or Keep waiting for the A/B tail is dropped
    // INV-LC14: a press that waited starts with the ticket of its press; a new press takes the latest one.
    const auto waited = std::exchange (liveCompareStartPressed, std::nullopt);
    const auto pressTicket = waited.value_or (processorRef.restoreOrPairTicket());
    StartResult result = StartResult::notReady;
    std::uint64_t generation = 0;
    hypha::live_compare_ui::performAction (hypha::live_compare_ui::ActionFaultScope::duringAction,
        [this, &result, &generation, waited]
        {
            result = processorRef.startLiveCompare (waited);
            generation = processorRef.liveCompareStatus().sessionGeneration;
            liveCompareMatched = liveCompareLimited = liveCompareInterruptSeen = false;
            liveCompareActiveSeen = result == StartResult::started;
            liveCompareAuto = {};
            return result == StartResult::started ? hypha::live_compare_ui::ActionOutcome::success
                                                  : hypha::live_compare_ui::ActionOutcome::refusal;
        }, [this] { return refreshLiveCompare(); },
        [this, &generation]
        { return hypha::live_compare_ui::currentNamedAction (processorRef.liveCompareStatus(), generation, false); },
        [this, &result, waited, pressTicket]
        {
            using hypha::live_compare_ui::StartAfterReturn;
            if (waited && result != StartResult::started && processorRef.restoreOrPairTicket() != *waited)
                return;  // restored or re-paired since the press: the press is dropped, not refused
            if (! waited && result != StartResult::started
                && hypha::live_compare_ui::onStartPressed (result) == StartAfterReturn::returnFirst)
            {
                requestNormalLevelReturn();  // the same RETURN, then LISTEN on this press
                liveCompareStartAfterReturnUntil = nowSecs() + 10.0; afterTail = {};  // one waiting press at a time
                liveCompareStartAfterReturnKey = waitingPairKey();
                liveCompareStartAfterReturnTicket = pressTicket;
                liveCompareStartAfterReturnBlind = false;
                showToast ("Returning POST to normal, then LISTEN");
                return;
            }
            if (result != StartResult::started) showToast (hypha::live_compare_ui::startFailure (result));
            if (hypha::live_compare_ui::startNeedsPreChoice (result)) showCandidateMenu();
        });
}

void KirinHyphaEditor::requestListenSelect (bool pre)
{
    processorRef.selectLiveComparePre (pre);
    liveCompareInterruptSeen = false;
    refreshLiveCompare();
}

void KirinHyphaEditor::requestListenMatch ()
{
    if (liveCompareMatched)
        chooseLiveCompareFollow();
    else
        matchLiveCompare();
}

void KirinHyphaEditor::requestListenEnd ()
{
    liveCompareStartAfterReturnUntil = 0.0;
    processorRef.finishLiveCompare();
    liveCompareFinishingSeen = true;
    liveCompareMatched = false;
    liveCompareLimited = false;
    liveCompareActiveSeen = false;
    liveCompareAuto = {};
    refreshLiveCompare();
}

void KirinHyphaEditor::requestNormalLevelReturn ()
{
    if (returnReferenceLevelIfHeld()) { refreshLiveCompare(); return; }  // Reference が下げた A を戻す
    processorRef.returnLiveComparePostToNormal();
    liveCompareFinishingSeen = true;
    refreshLiveCompare();
}

void KirinHyphaEditor::requestListenAutoMenu()
{
    const auto status = processorRef.liveCompareStatus();
    if (! status.active || status.finishing) { showToast ("LISTEN is not active"); return; }
    if (! status.matched && ! status.matchHeld) { showToast ("MATCH first"); return; }
    chooseLiveCompareFollow();
}
#endif
