#include "PluginEditor.h"

KirinHyphaEditor::~KirinHyphaEditor()
{
    stopTimer();
    processorRef.vuLive().setEnabled (false);
    setConstrainer (nullptr); // the size rule is a member and goes before the base editor
    pairPreview.reset();
    processorRef.setReferenceViewPresented (false);
    releaseAppearanceVisibility();
    commitEditorSizeStateIfSettled (true);
    tooltip.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
    if (isPost)
    {
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        if (localBlindOpen) processorRef.cancelLocalBlindProductSession();
        // LISTEN and live BLIND belong to the processor; closing its window is not END. AUTO rests
        // with the processor and a window of the same LISTEN continues it (INV-LC16). The rest is
        // keyed to the LISTEN and the restore and pair ticket: a loop wrap or a playback gap keeps it.
        processorRef.liveCompareAutoRest() = { liveCompareAuto.on, liveCompareAuto.approvedPreDb,
            liveCompareAuto.ceilingDbtp, processorRef.liveCompareListenSession(), processorRef.restoreOrPairTicket() };
       #endif
        processorRef.endReferenceBlind();
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        attackView.retireV2();
        processorRef.endAnalysisUiSession (analysisOwnerToken);
        analysisOwnerToken = 0;
       #endif
    }
}

void KirinHyphaEditor::timerCallback()
{
    // Some DAWs retain a hidden editor. UI acquisition belongs only to a showing editor;
    // always-on Meter Session, Record and their workers continue independently.
    // A visible detached editor is also used by native hosts/tests before its peer is attached.
    // Once attached, ancestor visibility and minimisation are part of the showing contract.
    if (!isVisible() || (getPeer() != nullptr && !isShowing()))
    {
        pairPreview.reset();
        pairPreviewShown = {};
        pairPreviewObservedGeneration = 0;
        processorRef.setReferenceViewPresented (false);
        processorRef.vuLive().setEnabled (false);
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        // AUTO rests while hidden: nothing below runs, so it neither measures nor moves (INV-LC16).
        // A Keep, LISTEN or MATCH press that has not started yet is dropped, never run later.
        dropWaitingRequests();
        syncAnalysisDemand();
       #endif
        return;
    }
    observatoryView.advanceTimePresentation (juce::Time::getMillisecondCounterHiRes());
    // The Hybrid VU needle follows the audio thread's own 300 ms integration at frame rate
    // (HyphaVuLiveTap.h); the 100 ms meter publication stays the footer's and Capture's source.
    processorRef.vuLive().setEnabled (observatoryView.hybridVuVisible());
    if (! observatoryView.onVuLiveLevel)
        observatoryView.onVuLiveLevel = [this] (int channel) { return processorRef.vuLive().level (channel); };
    syncLanguage();
    refreshAppearance();
    commitEditorSizeStateIfSettled (false);
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    const auto attachment = processorRef.takeCaptureWorkAttachmentResult();
    if (attachment.state == hypha::capture::WorkAttachmentResultState::attached)
        showToast ("Capture attached to Work");
    else if (attachment.state == hypha::capture::WorkAttachmentResultState::rejected)
    {
        if (attachment.code == "work_binding_changed")
            showToast ("Work connection changed; Capture was not attached");
        else if (attachment.code == "work_not_found")
            showToast ("Connected Work is no longer available");
        else if (attachment.code == "work_record_invalid"
                 || attachment.code == "work_write_failed"
                 || attachment.code == "destination_write_failed")
            showToast ("Work could not be updated");
        else if (attachment.code == "request_write_failed"
                 || attachment.code == "artifact_invalid"
                 || attachment.code == "request_invalid")
            showToast ("Capture could not be attached");
        else
            showToast ("Kirin OS could not attach this Capture");
    }
#endif
    if (isPost) updatePost();
    else        updatePre();
    refreshObservatory();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    const bool liveFault = isPost && refreshLiveCompare();
    if (isPost) { serviceLiveCompareStartAfterReturn(); serviceReferenceRematch(); requestKeepPair (true); }
   #endif
    refreshPairPreview (false);
    updateHelpLine();
    refreshUpdateChecking();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (isPost && liveCompareAuto.hiddenStopNoticePending)
    {
        // Queued when a reopened window could not continue the AUTO that rested (INV-LC16). It waits
        // for another toast; a newer action, AUTO again or an interruption notice supersedes it.
        if (liveFault || liveCompareAuto.on)
            liveCompareAuto.hiddenStopNoticePending = false;
        else if (toastText.isEmpty())
        {
            liveCompareAuto.hiddenStopNoticePending = false;
            showToast ("AUTO stopped: editor hidden");
        }
    }
   #endif
}

void KirinHyphaEditor::commitEditorSizeStateIfSettled (bool force)
{
    constexpr double settleSeconds = 0.2;
    if (! editorSizeStateDirty
        || (! force && nowSecs() - editorSizeLastChangedAt < settleSeconds))
        return;
    editorSizeStateDirty = false;
    processorRef.notifyObservatoryEditorSizeChanged();
}
