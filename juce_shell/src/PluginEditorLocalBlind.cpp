#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

namespace
{
using Phase = hypha::local_blind::ProductSessionPhase;

bool finished (Phase phase) noexcept
{
    return phase == Phase::idle || phase == Phase::returned || phase == Phase::failed;
}
}

void KirinHyphaEditor::configureLocalBlindProduct()
{
    const auto productSupported = processorRef.localBlindProductSupported();
    observatoryView.setLocalBlindEntryEnabled (
        hypha::local_blind_ui::productEntryEnabled (processorRef.wrapperType));
    observatoryView.onLocalBlind = [this] { openLocalBlindProduct(); };
    localBlindView.setMeterContext (processorRef.meterContextPreference());

    localBlindView.onCapture = [this] { beginLocalBlindProductCapture(); };
    localBlindView.onContextMenu = [this]
    { showMeterContextMenu (localBlindView.contextAnchor()); };
    localBlindView.onStart = [this] (bool approveLowerPost)
    {
        if (! processorRef.startLocalBlindProductTrial (approveLowerPost))
            localBlindView.setActionNotice ("BLIND COMPARE COULD NOT START");
        refreshLocalBlindProduct();
    };
    localBlindView.onSelectStimulus = [this] (int stimulus)
    {
        if (! processorRef.selectLocalBlindProductStimulus (stimulus))
            localBlindView.setActionNotice ("SOURCE COULD NOT BE SELECTED");
        refreshLocalBlindProduct();
    };
    localBlindView.onAnswer = [this] (hypha::local_blind::TrialAnswer answer)
    {
        if (! processorRef.answerLocalBlindProductTrial (answer))
            localBlindView.setActionNotice ("LISTEN TO BOTH COMPLETE PASSES FIRST");
        refreshLocalBlindProduct();
    };
    localBlindView.onReveal = [this]
    {
        if (! processorRef.revealLocalBlindProductTrial())
            localBlindView.setActionNotice ("CHOOSE AN ANSWER AFTER BOTH PASSES");
        refreshLocalBlindProduct();
    };
    localBlindView.onStop = [this]
    {
        processorRef.cancelLocalBlindProductSession();
        refreshLocalBlindProduct();
    };
    localBlindView.onReturn = [this]
    {
        processorRef.requestLocalBlindNormalReturn();
        refreshLocalBlindProduct();
    };
    localBlindView.onClose = [this] { closeLocalBlindProduct(); };
    scaleRoot.addChildComponent (localBlindView);

    const auto current = processorRef.localBlindProductView();
    localBlindOpen = productSupported
        && hypha::local_blind_ui::needsRecoveryScreen (current);
    localBlindView.setState (current);
}

void KirinHyphaEditor::openLocalBlindProduct()
{
    if (! isPost
        || ! hypha::local_blind_ui::productEntryEnabled (processorRef.wrapperType))
        return;
    const auto existing = processorRef.localBlindProductView();
    if (hypha::local_blind_ui::needsRecoveryScreen (existing))
    {
        localBlindPreflight = false;
        localBlindOpen = true;
        refreshLocalBlindProduct();
        return;
    }
    if (processorRef.pairStatus() != KIRIN_PAIR_STATUS_PAIRED)
    {
        showToast ("Select one exact PRE pair before Blind Compare");
        return;
    }
    if (processorRef.isRecording() || processorRef.keepPhase() != KIRIN_KEEP_PHASE_IDLE)
    {
        showToast ("Blind Compare is available after the current Keep or Record");
        return;
    }
    if (processorRef.referenceAuditionSnapshot().blindPhase
        != hypha::reference_audition::BlindPhase::inactive)
    {
        showToast ("End Reference Blind Compare before starting PRE / POST Blind");
        return;
    }
    localBlindPreflight = true;
    localBlindOpen = true;
    localBlindView.clearActionNotice();
    refreshLocalBlindProduct();
    localBlindView.grabKeyboardFocus();
}

void KirinHyphaEditor::beginLocalBlindProductCapture()
{
    if (! localBlindOpen || ! localBlindPreflight) return;
    if (processorRef.pairStatus() != KIRIN_PAIR_STATUS_PAIRED)
    {
        localBlindView.setActionNotice ("PAIR CHANGED / RETURN AND REOPEN");
        return;
    }
    if (processorRef.isRecording() || processorRef.keepPhase() != KIRIN_KEEP_PHASE_IDLE)
    {
        localBlindView.setActionNotice ("END KEEP / RECORD BEFORE CAPTURE");
        return;
    }
    if (! processorRef.isPlaying() || ! processorRef.heartbeatLive())
    {
        localBlindView.setActionNotice ("START PLAYBACK BEFORE CAPTURE");
        return;
    }
    if (processorRef.referenceAuditionSnapshot().blindPhase
        != hypha::reference_audition::BlindPhase::inactive)
    {
        localBlindView.setActionNotice ("END REFERENCE BLIND BEFORE CAPTURE");
        return;
    }
    const auto previousPhase = processorRef.localBlindProductView().phase;
    if (processorRef.requestLocalBlindProductCapture())
        localBlindPreflight = false;
    else
    {
        localBlindPreflight = processorRef.localBlindProductView().phase == previousPhase;
        localBlindView.setActionNotice ("ANALYSIS SLOT NOT AVAILABLE");
    }
    refreshLocalBlindProduct();
}

void KirinHyphaEditor::closeLocalBlindProduct()
{
    if (! finished (processorRef.localBlindProductView().phase))
        return;
    localBlindPreflight = false;
    localBlindOpen = false;
    localBlindView.setVisible (false);
    setLocalBlindIsolation (false);
    resized();
    refreshObservatory();
}

void KirinHyphaEditor::refreshLocalBlindProduct()
{
    const auto current = processorRef.localBlindProductView();
    if (processorRef.localBlindProductSupported()
        && ! localBlindOpen && hypha::local_blind_ui::needsRecoveryScreen (current))
        localBlindOpen = true;
    localBlindView.setMeterContext (processorRef.meterContextPreference());
    localBlindView.setState (localBlindPreflight
        ? hypha::local_blind::ProductSessionView {} : current);
    layoutLocalBlindProduct();
}

void KirinHyphaEditor::layoutLocalBlindProduct()
{
    localBlindView.setBounds (scaleRoot.getLocalBounds());
    localBlindView.setVisible (localBlindOpen);
    setLocalBlindIsolation (localBlindOpen);
    syncAnalysisDemand();
    if (localBlindOpen)
    {
        localBlindView.toFront (true);
        localBlindView.setAccessible (true);
    }
}

void KirinHyphaEditor::setLocalBlindIsolation (bool active)
{
    if (active && localBlindUnderlyingStates.empty())
    {
        tooltip.hideTip();
        for (int index = 0; index < scaleRoot.getNumChildComponents(); ++index)
        {
            auto* component = scaleRoot.getChildComponent (index);
            if (component == &localBlindView)
                continue;
            localBlindUnderlyingStates.push_back (
                { component, component->isAccessible(), component->isEnabled() });
            component->setAccessible (false);
            component->setEnabled (false);
        }
        return;
    }
    if (! active && ! localBlindUnderlyingStates.empty())
    {
        for (const auto& state : localBlindUnderlyingStates)
        {
            state.component->setEnabled (state.enabled);
            state.component->setAccessible (state.accessible);
        }
        localBlindUnderlyingStates.clear();
    }
}

#endif
