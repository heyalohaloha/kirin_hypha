#include "PluginEditor.h"
#include "HyphaLiveCompareActionResult.h"
#include "HyphaLiveCompareRecoveryText.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>

// Stage 3, AUTO (plan 6.2, INV-LC16). After an explicit MATCH, PRE's gain may follow POST's loudness:
// every second of proven playback the latest window is measured again, and PRE moves to the new
// match, ramped over 50 ms on the Audio Thread, once it is 0.5 dB or more away. AUTO keeps the
// ceiling the MATCH approved, never moves POST, and never strays more than 6 dB from that MATCH; at
// a limit it stays there, keeps following and says how far PRE is from the match. Silence or a failed
// measurement changes nothing. AUTO never reaches Blind: PIN, END and the end of the session stop it.
// The values are the plan's experimental ones until listening decides them; the screen names the
// tolerance.
namespace
{
namespace ui = hypha::ui_contract;
using hypha::live_compare::FollowAction;
using hypha::live_compare::FollowLimit;
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
        menu.addItem (3, processorRef.candidateCompareActive() ? "AUTO: follow B within 0.5 dB" : "AUTO: follow POST within 0.5 dB");
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.liveMatchAnchor()).withDeletionCheck (*this)
        .withMinimumWidth (juce::jlimit (300, 520, getWidth()))
        .withMaximumNumColumns (1).withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (options, [safe] (int result)
    {
        if (safe != nullptr) safe->handleLiveCompareFollowChoice (result);
    });
}

void KirinHyphaEditor::handleLiveCompareFollowChoice (int result)
{
    if (liveBlindOpen || result < 1 || result > 3) return;
    const auto opening = processorRef.liveCompareStatus();
    if (! opening.active || opening.finishing) return;
    if (result == 1) { matchLiveCompare(); return; }
    if (result == 2) { stopLiveCompareAuto ({}); showToast ("AUTO off"); return; }
    juce::String refusal;
    const auto completion = hypha::live_compare_ui::performAction (
        hypha::live_compare_ui::ActionFaultScope::includingOpeningRefresh,
        [] { return hypha::live_compare_ui::ActionOutcome::success; },
        [this] { return refreshLiveCompare(); },
        [this, &refusal]
        {
            const auto current = processorRef.liveCompareStatus();
            const auto readiness = hypha::live_compare_ui::autoReadiness (current, current.sessionGeneration);
            refusal = readiness == hypha::live_compare_ui::AutoReadiness::blocked
                ? hypha::live_compare_ui::namedRecovery (current)
                : hypha::live_compare_ui::autoReadinessNotice (readiness);
            return readiness == hypha::live_compare_ui::AutoReadiness::ready;
        }, [this]
        {
            liveCompareAuto.on = true;
            liveCompareAuto.hiddenStopNoticePending = false;
            liveCompareAuto.limit = FollowLimit::none;
            liveCompareAuto.nextAt = nowSecs() + hypha::live_compare::followIntervalSeconds;
            showToast ("AUTO on: within 0.5 dB");
        });
    if (completion == hypha::live_compare_ui::ActionCompletion::ineligible && refusal.isNotEmpty())
        showToast (refusal);
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
    // END and BLIND end AUTO with them, and an interruption says its own reason; the others say why AUTO stopped.
    if (! status.active || status.finishing || liveBlindOpen || status.interrupted)
    {
        a.on = false;
        return;
    }
    if (status.matchLimited || (!status.matched && !status.matchHeld))
    {
        stopLiveCompareAuto (status.matchLimited ? "AUTO stopped at the TP limit" : "AUTO stopped: press MATCH again");
        return;
    }
    if (status.contentHeld || status.compensationOff || ! status.matchReady
        || status.observation != hypha::live_compare::RecoveryReason::none
        || status.verdict != hypha::live_compare::Verdict::accepted || now < a.nextAt)
        return;
    a.nextAt = now + hypha::live_compare::followIntervalSeconds;
    const double heldDb = status.postTarget > 0.0f ? 20.0 * std::log10 (status.postTarget) : 0.0;
    const double currentDb = status.gain > 0.0f ? 20.0 * std::log10 (status.gain) : 0.0;
    const auto measured = processorRef.measureLiveCompare();
    if (! measured.ok())
        return; // silence keeps the gain and what the line says
    const auto step = hypha::live_compare::followStep (measured, heldDb, a.approvedPreDb, a.ceilingDbtp, currentDb, a.peakDbtp);
    a.limit = step.limit;
    a.shortDb = step.shortDb;
    a.peakDbtp = step.peakDbtp;
    if (step.action == FollowAction::move && ! processorRef.followLiveCompareGain (step.preGainDb))
        a.limit = FollowLimit::none; // the session refused the step: the amount above never happened
}

#endif
