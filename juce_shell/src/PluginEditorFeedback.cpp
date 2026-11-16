#include "PluginEditor.h"

void KirinHyphaEditor::showToast (const juce::String& msg)
{
    automaticPairNotice = false;
    comparisonNoticeReason = KIRIN_COMPARISON_REASON_NONE;
    toastUntil = nowSecs() + 3.0; // TOAST_DURATION_SECS
    toastText = msg;
    updateFeedback (nowSecs(), false, {});
}

// A change Hypha made by itself says why, but never over a user's notice or a persistent error, and it
// is withdrawn when the change is undone (showing false).
void KirinHyphaEditor::showAutomaticNotice (const juce::String& text, bool showing)
{
    if (! showing)
    {
        pendingAutomaticNotices.removeString (text);
        if (toastText == text && text.isNotEmpty())
        {
            toastText.clear();
            toastUntil = 0.0;
            updateFeedback (nowSecs(), false, {});
        }
        return;
    }
    if (text.isEmpty()) return;
    // Hold the change until the notice line is free and a Blind screen no longer owns it.
    if (feedbackRead && pendingAutomaticNotices.isEmpty() && automaticNoticeMayShow()
        && (shownNotice.isEmpty() || shownNotice == text))
    {
        showToast (text);
    }
    else pendingAutomaticNotices.addIfNotAlreadyThere (text);
}

bool KirinHyphaEditor::automaticNoticeMayShow() const
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return ! blindRunning();
   #else
    return true;
   #endif
}
