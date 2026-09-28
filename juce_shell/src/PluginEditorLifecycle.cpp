#include "PluginEditor.h"

KirinHyphaEditor::~KirinHyphaEditor()
{
    stopTimer();
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
        processorRef.stopLiveCompare(); // a closed window never leaves PRE sounding (plan E2)
       #endif
        processorRef.endReferenceBlind();
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        processorRef.endAnalysisUiSession (analysisOwnerToken);
        analysisOwnerToken = 0;
       #endif
    }
}

void KirinHyphaEditor::timerCallback()
{
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
    if (isPost) refreshLiveCompare();
   #endif
    refreshPairPreview (false);
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
