#include "PluginEditor.h"

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
    liveBlindView.onAnswer = [this]
    {
        const auto trial = processorRef.liveBlindStatus().trial;
        if (trial.played != 3 || trial.revealed || trial.invalidated) return;
        juce::PopupMenu menu;
        menu.setLookAndFeel (&pairMenuLookAndFeel());
        menu.addItem (1, "PREFER SOURCE 1");
        menu.addItem (2, "PREFER SOURCE 2");
        menu.addItem (3, "NO PREFERENCE");
        menu.addItem (4, "CANNOT TELL");
        juce::Component::SafePointer<KirinHyphaEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&liveBlindView.answerAnchor())
            .withDeletionCheck (*this).withMinimumWidth (240).withMaximumNumColumns (1),
            [safe, epoch = trial.epoch] (int choice)
            {
                if (safe == nullptr || ! safe->liveBlindOpen || choice == 0
                    || safe->processorRef.liveBlindStatus().trial.epoch != epoch) return;
                safe->processorRef.answerLiveBlind (choice);
                safe->refreshLiveBlind();
            });
    };
}

void KirinHyphaEditor::openLiveBlind()
{
    if (localBlindOpen || liveBlindOpen) return;
    liveCompareAuto = {}; // freeze the last explicit MATCH; AUTO never enters Blind
    const auto result = processorRef.beginLiveBlind();
    if (result != StartResult::started)
    {
        if (result == StartResult::noPair) showCandidateMenu();
        else showToast (result == StartResult::returnRequired ? "Press RETURN first"
                      : result == StartResult::comparisonBusy ? "End the current comparison first"
                                                            : "BLIND COMPARE COULD NOT START");
        return;
    }
    liveBlindOpen = true;
    juce::PopupMenu::dismissAllActiveMenus();
    // Isolate every measurement, tooltip and accessibility sibling BEFORE publishing an anonymous source.
    layoutLocalBlindProduct();
    refreshLiveBlind();
    liveBlindView.grabKeyboardFocus();
}

void KirinHyphaEditor::refreshLiveBlind()
{
    if (! liveBlindOpen) return;
    monitorLiveCompareOffset (processorRef.liveCompareStatus(), nowSecs());
    processorRef.serviceLiveBlind();
    const auto status = processorRef.liveCompareStatus();
    const auto blind = processorRef.liveBlindStatus();
    if (blind.stage == BlindStage::idle && ! status.finishing)
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
    if (! live.active) menu.addItem (40, "LISTEN", processorRef.liveCompareAdmission (false) == StartResult::started);
    else
    {
        menu.addItem (41, "MATCH", ! live.finishing);
        menu.addItem (42, "PRE", ! live.finishing);
        menu.addItem (43, "POST", ! live.finishing);
        menu.addItem (44, "PIN 4 S", ! live.finishing);
    }
    if (observatoryView.localBlindEntryAvailable())
        menu.addItem (23, "BLIND", ! keepActive && processorRef.liveCompareAdmission (true) == StartResult::started
            && (! live.active || live.matchReady));
}

bool KirinHyphaEditor::handleLiveCompareMenu (int result)
{
    if (result < 40 || result > 44) return false;
    if (result == 40 && observatoryView.onLiveCompareStart) observatoryView.onLiveCompareStart();
    if (result == 41 && observatoryView.onLiveCompareMatch) observatoryView.onLiveCompareMatch();
    if (result == 42 || result == 43) processorRef.selectLiveComparePre (result == 42);
    if (result == 44) pinLiveCompareForBlind();
    return true;
}
#endif
