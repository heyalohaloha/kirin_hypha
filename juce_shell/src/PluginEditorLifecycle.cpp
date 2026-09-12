#include "PluginEditor.h"

KirinHyphaEditor::~KirinHyphaEditor()
{
    stopTimer();
    releaseAppearanceVisibility();
    commitEditorSizeStateIfSettled (true);
    tooltip.setLookAndFeel (nullptr);
    if (isPost)
    {
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        if (localBlindOpen) processorRef.cancelLocalBlindProductSession();
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
