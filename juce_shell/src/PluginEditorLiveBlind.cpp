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

void KirinHyphaEditor::openLiveBlind()
{
    if (localBlindOpen || liveBlindOpen) return;
    liveCompareAuto = {}; // freeze the last explicit MATCH; AUTO never enters Blind
    const auto result = processorRef.beginLiveBlind();
    if (result != StartResult::started)
    {
        refreshLiveCompare(); // acknowledge older transitions before this direct refusal
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
        return;
    }
    liveBlindView.setState (blind, processorRef.isPlaying(), status.postActual);
}

void KirinHyphaEditor::addLiveCompareMenu (juce::PopupMenu& menu, bool keepActive)
{
    const auto live = processorRef.liveCompareStatus();
    if (! processorRef.stereoWorkflowsSupported() || ! processorRef.liveCompareSupported()) return;
    menu.addSectionHeader ("PRE / POST");
    const auto recovery = hypha::live_compare_ui::currentListenPresentation (
        live, processorRef.liveCompareAdmission (false), processorRef.referenceOutputSlot());
    if (*recovery.instruction != 0) menu.addSectionHeader (recovery.instruction);
    if (! live.active) menu.addItem (40, "LISTEN", hypha::live_compare_ui::entryOffered (processorRef.liveCompareAdmission (false)));
    else
    {
        menu.addItem (41, "MATCH", ! live.finishing);
        menu.addItem (42, "PRE", ! live.finishing);
        menu.addItem (43, "POST", ! live.finishing);
    }
    if (observatoryView.localBlindEntryAvailable())
        menu.addItem (23, "BLIND", ! keepActive && processorRef.liveCompareAdmission (true) == StartResult::started);
}

bool KirinHyphaEditor::handleLiveCompareMenu (int result)
{
    // 1.1.52 shows no PIN 4 S entry (Exact 4 S); its internals leave in a later release.
    if (result < 40 || result > 43) return false;
    if (result == 40 && observatoryView.onLiveCompareStart) observatoryView.onLiveCompareStart();
    if (result == 41 && observatoryView.onLiveCompareMatch) observatoryView.onLiveCompareMatch();
    if (result == 42 || result == 43) processorRef.selectLiveComparePre (result == 42);
    return true;
}
#endif
