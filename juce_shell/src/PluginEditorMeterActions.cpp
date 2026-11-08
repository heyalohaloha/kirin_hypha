#include "PluginEditor.h"
#include "HyphaLanguage.h"

void KirinHyphaEditor::requestMeterSessionReset()
{
    if (refusedByBlind()) return;
    if (meterResetDialog != nullptr) return;
    meterResetDialog = std::make_unique<juce::AlertWindow> (
        "RESET", hypha::i18n::tr ("Reset Meter Session?"), juce::MessageBoxIconType::NoIcon, this);
    meterResetDialog->setLookAndFeel (&textLookAndFeel);
    meterResetDialog->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    meterResetDialog->addButton ("RESET", 1);
    meterResetDialog->centreAroundComponent (this, meterResetDialog->getWidth(), meterResetDialog->getHeight());
    const juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    const juce::Component::SafePointer<juce::AlertWindow> dialog (meterResetDialog.get());
    meterResetDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, dialog] (int result)
    {
        if (safe == nullptr || dialog == nullptr || safe->meterResetDialog.get() != dialog.getComponent()) return;
        safe->meterResetDialog.reset();
        if (result != 1 || ! safe->isShowing()) return;
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        if (safe->localBlindOpen || safe->liveBlindOpen || safe->processorRef.referenceAuditionSnapshot().blindPhase
            != hypha::reference_audition::BlindPhase::inactive) return;
       #endif
        safe->performMeterSessionReset();
    }), false);
    if (auto* cancel = meterResetDialog->getButton (0)) cancel->grabKeyboardFocus();
}

void KirinHyphaEditor::performMeterSessionReset()
{
    if (! processorRef.resetMeterSession())
    {
        showToast ("Meter Session could not be reset");
        return;
    }
    observatoryWatchDisplay = {};
    haveObservatoryWatchDisplay = false;
    observatoryView.setWatchDisplay ({}, false);
}

void KirinHyphaEditor::requestPeakClipClear()
{
    if (refusedByBlind()) return;
    if (! processorRef.clearMeterPeakClipHolds()) showToast ("TP / Clip CLEAR failed");
}
