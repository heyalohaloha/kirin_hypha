#include "PluginEditor.h"
#include "HyphaLiveCompareActionResult.h"
#include "HyphaLiveCompareRecoveryText.h"

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
        if (safe == nullptr || safe->liveBlindOpen || result < 1 || result > 3)
            return;
        const auto opening = safe->processorRef.liveCompareStatus();
        if (! opening.active || opening.finishing || opening.sessionGeneration != generation)
            return;
        if (result == 1)
        {
            safe->matchLiveCompare (generation); // the measurement/plan keeps the menu's scope
            return;
        }
        juce::String refusal;
        const auto completion = hypha::live_compare_ui::performAction (hypha::live_compare_ui::ActionFaultScope::includingOpeningRefresh,
            [safe, result, generation]
            {
                const auto current = safe->processorRef.liveCompareStatus();
                if (! hypha::live_compare_ui::currentNamedAction (current, generation, false))
                    return hypha::live_compare_ui::ActionOutcome::stale;
                if (result == 2) safe->stopLiveCompareAuto ({});
                return hypha::live_compare_ui::ActionOutcome::success;
            },
            [safe] { return safe->refreshLiveCompare(); },
            [safe, result, generation, &refusal]
            {
                const auto current = safe->processorRef.liveCompareStatus();
                if (result != 3) return hypha::live_compare_ui::currentNamedAction (current, generation, false);
                const auto readiness = hypha::live_compare_ui::autoReadiness (current, generation);
                refusal = readiness == hypha::live_compare_ui::AutoReadiness::blocked
                    ? hypha::live_compare_ui::namedRecovery (current)
                    : hypha::live_compare_ui::autoReadinessNotice (readiness);
                return readiness == hypha::live_compare_ui::AutoReadiness::ready;
            }, [safe, result]
            {
                if (result == 3)
                {
                    safe->liveCompareAuto.on = true;
                    safe->liveCompareAuto.nextAt = safe->nowSecs() + hypha::live_compare::followIntervalSeconds;
                }
                safe->showToast (result == 3 ? "AUTO on: within 0.5 dB" : "AUTO off");
            });
        if (completion == hypha::live_compare_ui::ActionCompletion::ineligible && refusal.isNotEmpty())
            safe->showToast (refusal); // same current intention failed; new faults/stale menus remain untouched
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
bool KirinHyphaEditor::followLiveCompare (const hypha::live_compare::Status& status, double now)
{
    auto& a = liveCompareAuto;
    if (! a.on)
        return false;
    if (! status.active || status.finishing || liveBlindOpen || ! status.matched || status.matchLimited
        || status.interrupted)
    {
        a.on = false;
        return false;
    }
    if (status.contentHeld || status.compensationOff || ! status.matchReady
        || status.verdict != hypha::live_compare::Verdict::accepted || now < a.nextAt)
        return false;
    a.nextAt = now + hypha::live_compare::followIntervalSeconds;
    const double heldDb = status.postTarget > 0.0f ? 20.0 * std::log10 (status.postTarget) : 0.0;
    const double currentDb = status.gain > 0.0f ? 20.0 * std::log10 (status.gain) : 0.0;
    const auto step = hypha::live_compare::followStep (processorRef.measureLiveCompare(), heldDb, a.approvedPreDb,
                                                       a.ceilingDbtp, currentDb);
    if (step.action == FollowAction::stopCeiling)
    {
        stopLiveCompareAuto ("AUTO stopped: TP ceiling");
        return true;
    }
    else if (step.action == FollowAction::stopReach)
    {
        stopLiveCompareAuto ("AUTO stopped: over 6 dB");
        return true;
    }
    else if (step.action == FollowAction::move)
        processorRef.followLiveCompareGain (step.preGainDb);
    return false;
}

#endif
