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
    observatoryView.setLocalBlindEntryEnabled (
        hypha::local_blind_ui::productEntryEnabled);
    observatoryView.onLocalBlind = [this] { openLocalBlindProduct(); };
    localBlindView.setMeterContext (processorRef.meterContextPreference());

    localBlindView.onCapture = [this] { beginLocalBlindProductCapture(); };
    localBlindView.onContextMenu = [this]
    { showMeterContextMenu (localBlindView.contextAnchor()); };
    localBlindView.onStart = [this] (bool approveLowerPost)
    {
        if (! processorRef.startLocalBlindProductTrial (approveLowerPost))
            showToast ("Blind Compare could not start from this state");
        refreshLocalBlindProduct();
    };
    localBlindView.onSelectStimulus = [this] (int stimulus)
    {
        if (! processorRef.selectLocalBlindProductStimulus (stimulus))
            showToast ("The hidden source could not be selected");
        refreshLocalBlindProduct();
    };
    localBlindView.onAnswer = [this] (hypha::local_blind::TrialAnswer answer)
    {
        if (! processorRef.answerLocalBlindProductTrial (answer))
            showToast ("Listen to one complete pass of both sources first");
        refreshLocalBlindProduct();
    };
    localBlindView.onReveal = [this]
    {
        if (! processorRef.revealLocalBlindProductTrial())
            showToast ("Choose an answer after hearing both complete passes");
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
    localBlindOpen = hypha::local_blind_ui::needsRecoveryScreen (current);
    localBlindView.setState (current);
}

void KirinHyphaEditor::openLocalBlindProduct()
{
    if (! isPost || ! hypha::local_blind_ui::productEntryEnabled)
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
    refreshLocalBlindProduct();
    localBlindView.grabKeyboardFocus();
}

void KirinHyphaEditor::beginLocalBlindProductCapture()
{
    if (! localBlindOpen || ! localBlindPreflight) return;
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
    if (! processorRef.isPlaying() || ! processorRef.heartbeatLive())
    {
        showToast ("Start playback, then capture the exact four second range");
        return;
    }
    if (processorRef.referenceAuditionSnapshot().blindPhase
        != hypha::reference_audition::BlindPhase::inactive)
    {
        showToast ("End Reference Blind Compare before starting PRE / POST Blind");
        return;
    }
    const auto previousPhase = processorRef.localBlindProductView().phase;
    if (processorRef.requestLocalBlindProductCapture())
        localBlindPreflight = false;
    else
    {
        localBlindPreflight = processorRef.localBlindProductView().phase == previousPhase;
        showToast ("Blind Compare could not reserve an Analysis slot");
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
    if (! localBlindOpen && hypha::local_blind_ui::needsRecoveryScreen (current))
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
