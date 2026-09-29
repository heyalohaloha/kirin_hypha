#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>

// Stage 3, AUTO (plan 6.2, INV-LC16). After an explicit MATCH, PRE's gain may follow POST's loudness:
// every second of proven playback the latest window is measured again, and PRE moves to the new
// match, ramped over 50 ms on the Audio Thread, once it is 0.5 dB or more away. AUTO keeps the
// ceiling the MATCH approved, never moves POST, and never strays more than 6 dB from that MATCH; it
// stops and says why instead. Silence or a failed measurement changes nothing. AUTO never reaches
// Blind: PIN, END and the end of the session stop it. The values are the plan's experimental ones
// until listening decides them; the screen names the tolerance.
namespace
{
namespace ui = hypha::ui_contract;
using hypha::live_compare::FollowAction;
}

void KirinHyphaEditor::chooseLiveCompareFollow()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addItem (1, "MATCH again");
    if (liveCompareAuto.on)
        menu.addItem (2, "Stop AUTO");
    else if (liveCompareLimited)
        menu.addItem (3, "AUTO needs a MATCH without TP LIMIT", false);
    else
        menu.addItem (3, "AUTO: follow POST within 0.5 dB");
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.liveMatchAnchor()).withDeletionCheck (*this)
        .withMinimumWidth (juce::jlimit (300, 520, getWidth()))
        .withMaximumNumColumns (1).withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    const auto generation = processorRef.liveCompareStatus().sessionGeneration;
    menu.showMenuAsync (options, [safe, generation] (int result)
    {
        if (safe == nullptr || safe->liveBlindOpen || safe->processorRef.liveCompareStatus().finishing
            || safe->processorRef.liveCompareStatus().sessionGeneration != generation)
            return;
        if (result == 1)
            safe->matchLiveCompare();
        else if (result == 2)
            safe->stopLiveCompareAuto ("AUTO off");
        else if (result == 3 && safe->liveCompareMatched && ! safe->liveCompareLimited)
        {
            safe->liveCompareAuto.on = true;
            safe->liveCompareAuto.nextAt = safe->nowSecs() + hypha::live_compare::followIntervalSeconds;
            safe->showToast ("AUTO on: within 0.5 dB");
        }
        safe->refreshLiveCompare();
    });
}

void KirinHyphaEditor::stopLiveCompareAuto (const juce::String& notice)
{
    liveCompareAuto.on = false;
    if (notice.isNotEmpty())
        showToast (notice);
}

// Message thread, from the refresh. A held content offset, a block that is not proven or a window
// that does not measure leaves the gain where it is.
void KirinHyphaEditor::followLiveCompare (const hypha::live_compare::Status& status, double now)
{
    auto& a = liveCompareAuto;
    if (! a.on)
        return;
    if (! status.active || status.finishing || liveBlindOpen || ! status.matched || status.matchLimited)
    {
        a.on = false;
        return;
    }
    if (status.contentHeld || status.verdict != hypha::live_compare::Verdict::accepted || now < a.nextAt)
        return;
    a.nextAt = now + hypha::live_compare::followIntervalSeconds;
    const double heldDb = status.postTarget > 0.0f ? 20.0 * std::log10 (status.postTarget) : 0.0;
    const double currentDb = status.gain > 0.0f ? 20.0 * std::log10 (status.gain) : 0.0;
    const auto step = hypha::live_compare::followStep (processorRef.measureLiveCompare(), heldDb, a.approvedPreDb,
                                                       a.ceilingDbtp, currentDb);
    if (step.action == FollowAction::stopCeiling)
        stopLiveCompareAuto ("AUTO stopped: TP ceiling");
    else if (step.action == FollowAction::stopReach)
        stopLiveCompareAuto ("AUTO stopped: over 6 dB");
    else if (step.action == FollowAction::move)
        processorRef.followLiveCompareGain (step.preGainDb);
}

#endif
