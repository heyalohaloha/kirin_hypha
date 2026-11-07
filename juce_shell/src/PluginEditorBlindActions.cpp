#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY
bool KirinHyphaEditor::blindRunning() const
{
    return localBlindOpen || liveBlindOpen || processorRef.referenceAuditionSnapshot().blindPhase
        != hypha::reference_audition::BlindPhase::inactive;
}

hypha::blind_ui::ScreenComponent* KirinHyphaEditor::activeBlindScreen()
{
    if (localBlindOpen) return nullptr;
    if (liveBlindOpen) return &liveBlindView;
    return blindRunning() ? &versionBlindView : nullptr;
}

void KirinHyphaEditor::requestBlindStart()
{
    if (blindRunning()) return;
    if (observatoryDomain == hypha::observatory::Domain::reference)
    {
        showActionNotice (referenceView.requestVersionBlind());
        return;
    }
    // MENU offers BLIND on the same terms.
    const bool keeping = processorRef.isRecording() || keepPreWaitSince > 0.0
        || processorRef.keepPhase() != (int) KIRIN_KEEP_PHASE_IDLE;
    if (keeping || ! observatoryView.localBlindEntryAvailable())
        showActionNotice ("BLIND COMPARE COULD NOT START");
    else
        openLiveBlind();
}

void KirinHyphaEditor::requestBlindSource (int source)
{
    if (localBlindOpen)
    {
        if (! localBlindView.requestSource (source)) localBlindView.setActionNotice ("SOURCE COULD NOT BE SELECTED");
    }
    else if (auto* screen = activeBlindScreen())
    {
        if (! screen->requestSource (source)) screen->setActionNotice ("SOURCE COULD NOT BE SELECTED");
    }
}

void KirinHyphaEditor::requestBlindReveal()
{
    if (localBlindOpen)
    {
        if (! localBlindView.requestReveal()) localBlindView.setActionNotice ("CHOOSE AN ANSWER AFTER BOTH PASSES");
    }
    else if (auto* screen = activeBlindScreen())
    {
        if (! screen->requestReveal()) screen->setActionNotice ("Hear both sources first");
    }
}

void KirinHyphaEditor::requestBlindEnd()
{
    if (localBlindOpen) localBlindView.requestEnd();
    else if (auto* screen = activeBlindScreen()) screen->requestEnd();
}
#endif
