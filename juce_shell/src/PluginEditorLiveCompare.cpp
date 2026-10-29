#include "PluginEditor.h"
#include "HyphaLiveCompareActionResult.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaReferenceAuditionRecoveryText.h"
#include "HyphaLocalBlindAdmissionText.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>
#include <cstdlib>

// The editor side of the live PRE / POST compare (AGENTS R-12, INV-LC1 to INV-LC16). It starts and
// ends the session, forwards PRE / POST, MATCH and AUTO, asks before lowering POST, and shows RT
// receipts. Explicit END restores unity; window closure keeps the processor-owned comparison.
namespace
{
namespace ui = hypha::ui_contract;
using hypha::live_compare::MatchChoice;
using hypha::live_compare::MatchFailure;
using hypha::live_compare::MatchPlan;
using hypha::live_compare::StartResult;

juce::String signedDb (double db)
{
    const auto hundredths = juce::roundToInt (db * 100.0);
    if (hundredths == 0)
        return "0.00 dB";
    const auto magnitude = std::abs (hundredths);
    return juce::String (hundredths > 0 ? "+" : "-") + juce::String (magnitude / 100) + "."
         + juce::String (magnitude % 100).paddedLeft ('0', 2) + " dB";
}

juce::String magnitudeDb (double db)
{
    return signedDb (std::fabs (db)).trimCharactersAtStart ("+");
}

// INV-LC7: the offset as a fact, without a judgement (R-22), in milliseconds at the rate the ring
// counts in. The host's rate is not read here: a processor prepared without it reports none.
juce::String offsetText (std::int64_t lag, double sampleRate)
{
    if (sampleRate <= 0.0)
        return {};
    const auto ms = std::fabs (static_cast<double> (lag)) * 1000.0 / sampleRate;
    return "PRE " + juce::String (ms, 2) + (lag < 0 ? " ms early" : " ms late");
}

juce::String startFailure (StartResult result)
{
    switch (result)
    {
        case StartResult::started:
        case StartResult::notPost:           return {};
        case StartResult::contentHeld:       return "Timing changed: check chain delay (POST)";
        case StartResult::comparisonBusy:    return "End the current comparison first";
        case StartResult::returnPending:     return "Ended; normal level returns with audio";
        case StartResult::returnRequired:    return "Press RETURN first";
        case StartResult::notReady:          return "LISTEN could not start";
        case StartResult::noPair:            return "Choose the PRE first";
        case StartResult::unsupportedLayout: return "Mono / stereo only";
        case StartResult::preUnavailable:    return hypha::live_compare_ui::preUnavailableNotice();
        case StartResult::preMultiMono:      return "PRE is multi-mono: insert it as stereo";
    }
    return {};
}

juce::String matchFailure (MatchFailure failure)
{
    switch (failure)
    {
        case MatchFailure::none:            return {};
        case MatchFailure::notProven:
        case MatchFailure::tooShort:
        case MatchFailure::overwritten:
        case MatchFailure::notEnoughSignal: return {};
        case MatchFailure::outOfRange:      return "MATCH over 24 dB";
        case MatchFailure::stale:
        case MatchFailure::invalidPlan:     return "MATCH failed; try again";
    }
    return {};
}
}

void KirinHyphaEditor::configureLiveCompare()
{
    observatoryView.onLiveCompareStart = [this]
    {
        StartResult result = StartResult::notReady;
        std::uint64_t generation = 0;
        hypha::live_compare_ui::performAction (hypha::live_compare_ui::ActionFaultScope::duringAction,
            [this, &result, &generation]
            {
                result = processorRef.startLiveCompare();
                generation = processorRef.liveCompareStatus().sessionGeneration;
                liveCompareMatched = liveCompareLimited = liveCompareInterruptSeen = false;
                liveCompareActiveSeen = result == StartResult::started;
                liveCompareAuto = {};
                return result == StartResult::started ? hypha::live_compare_ui::ActionOutcome::success
                                                      : hypha::live_compare_ui::ActionOutcome::refusal;
            }, [this] { return refreshLiveCompare(); },
            [this, &generation]
            { return hypha::live_compare_ui::currentNamedAction (processorRef.liveCompareStatus(), generation, false); },
            [this, &result]
            {
                if (result != StartResult::started) showToast (startFailure (result));
                if (hypha::live_compare_ui::startNeedsPreChoice (result)) showCandidateMenu();
            });
    };
    observatoryView.onLiveCompareSelect = [this] (bool pre)
    {
        processorRef.selectLiveComparePre (pre);
        liveCompareInterruptSeen = false;
        refreshLiveCompare();
    };
    // Once matched, MATCH offers MATCH again and AUTO (INV-LC16); the first MATCH measures at once.
    observatoryView.onLiveCompareMatch = [this]
    {
        if (liveCompareMatched)
            chooseLiveCompareFollow();
        else
            matchLiveCompare();
    };
    observatoryView.onLiveCompareEnd = [this]
    {
        processorRef.finishLiveCompare();
        liveCompareFinishingSeen = true;
        liveCompareMatched = false;
        liveCompareLimited = false;
        liveCompareActiveSeen = false;
        liveCompareAuto = {};
        refreshLiveCompare();
    };
    observatoryView.onLiveCompareReturn = [this]
    {
        if (returnReferenceLevelIfHeld()) { refreshLiveCompare(); return; }  // Reference が下げた A を戻す
        processorRef.returnLiveComparePostToNormal();
        liveCompareFinishingSeen = true;
        refreshLiveCompare();
    };
}

// The explicit MATCH: measure the latest window and apply it, asking first when PRE would pass the
// true-peak ceiling.
void KirinHyphaEditor::matchLiveCompare (std::uint64_t menuGeneration)
{
    if (menuGeneration != UINT64_MAX)
    {
        refreshLiveCompare(); // service may invalidate the menu before measurement
        if (! hypha::live_compare_ui::currentNamedAction (processorRef.liveCompareStatus(), menuGeneration, false))
            return;
    }
    const auto result = processorRef.measureLiveCompare();
    if (! result.ok())
    {
        const bool waiting = result.failure == MatchFailure::notProven
            || result.failure == MatchFailure::notEnoughSignal || result.failure == MatchFailure::stale
            || result.failure == MatchFailure::tooShort || result.failure == MatchFailure::overwritten;
        if (waiting) processorRef.requestLiveCompareMatch();
        else showToast (matchFailure (result.failure));
        return;
    }
    if (menuGeneration != UINT64_MAX && (! result.generationBound || result.generation != menuGeneration))
        return; // RT may change the generation while measuring; never approve the replacement session
    const auto held = processorRef.liveCompareStatus().postTarget;
    const auto plan = hypha::live_compare::planMatch (result, held > 0.0f ? 20.0 * std::log10 (held) : 0.0);
    if (plan.failure != MatchFailure::none)
    {
        refreshLiveCompare();
        showToast (matchFailure (plan.failure));
        return;
    }
    if (plan.needsApproval)
        chooseLiveCompareMatch (plan);
    else
        applyLiveCompareChoice (plan, MatchChoice::basis);
}

// PRE would pass the true-peak ceiling on the POST basis: the user chooses, as in Local Blind,
// between lowering POST (PRE at its level) and raising PRE only up to the ceiling. Dismissing the
// menu changes nothing.
void KirinHyphaEditor::chooseLiveCompareMatch (const MatchPlan& plan)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("PRE needs " + signedDb (plan.neededPreGainDb) + "; TP ceiling allows "
                           + signedDb (plan.limitedPreGainDb));
    menu.addItem (1, "Lower POST by " + magnitudeDb (plan.lowerPostGainDb) + "; PRE stays at its level");
    menu.addItem (2, "Raise PRE by " + signedDb (plan.limitedPreGainDb) + " only (TP LIMIT)");
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.liveMatchAnchor()).withDeletionCheck (*this)
        .withMinimumWidth (juce::jlimit (300, 520, getWidth()))
        .withMaximumNumColumns (1).withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (options, [safe, plan] (int result)
    {
        if (safe != nullptr) safe->handleLiveCompareMatchChoice (plan, result);
    });
}

void KirinHyphaEditor::handleLiveCompareMatchChoice (const MatchPlan& plan, int result)
{
    if (! liveBlindOpen && (result == 1 || result == 2))
        applyLiveCompareChoice (plan, result == 1 ? MatchChoice::lowerPost : MatchChoice::limitPre);
}

void KirinHyphaEditor::applyLiveCompareChoice (const MatchPlan& plan, MatchChoice choice)
{
    hypha::live_compare::MatchApplication applied;
    std::uint64_t generation = 0;
    hypha::live_compare_ui::performAction (hypha::live_compare_ui::ActionFaultScope::duringAction,
        [this, &plan, choice, &applied, &generation]
        {
            generation = processorRef.liveCompareStatus().sessionGeneration;
            applied = processorRef.applyLiveCompareMatch (plan, choice);
            if (applied.failure == MatchFailure::stale)
            {
                processorRef.requestLiveCompareMatch();
                return hypha::live_compare_ui::ActionOutcome::stale;
            }
            if (! applied) return hypha::live_compare_ui::ActionOutcome::refusal;
            liveCompareMatched = true;
            liveCompareLimited = choice == MatchChoice::limitPre;
            // AUTO keeps this explicit MATCH's point; it cannot follow a TP LIMIT.
            liveCompareAuto.approvedPreDb = choice == MatchChoice::lowerPost ? 0.0 : plan.preGainDb;
            liveCompareAuto.ceilingDbtp = plan.ceilingDbtp;
            liveCompareAuto.nextAt = nowSecs() + 1.0;
            liveCompareAuto.on = liveCompareAuto.on && ! liveCompareLimited;
            return hypha::live_compare_ui::ActionOutcome::success;
        }, [this] { return refreshLiveCompare(); },
        [this, &generation]
        { return hypha::live_compare_ui::currentNamedAction (processorRef.liveCompareStatus(), generation, true); },
        [this, &plan, choice, &applied]
        {
            showToast (! applied ? matchFailure (applied.failure)
                : choice == MatchChoice::lowerPost ? "MATCH: POST " + signedDb (plan.lowerPostGainDb)
                : choice == MatchChoice::limitPre
                    ? "TP limit: PRE " + signedDb (plan.limitedPreGainDb) + ", need " + signedDb (plan.neededPreGainDb)
                    : "MATCH: PRE " + signedDb (plan.preGainDb));
        });
}

// Another audition must not start on top of an approved POST attenuation: RETURN first.
bool KirinHyphaEditor::monitorLiveCompareOffset (const hypha::live_compare::Status& status, double now)
{
    juce::ignoreUnused (now);
    const auto observed = processorRef.liveContentTimingObservation();
    liveCompareWarning = status.prolongedWait && status.contentHeld
        ? "Timing changed: check chain delay (POST)"
        : status.active && observed.settled && std::llabs (observed.lagFrames) > 1
            ? offsetText (observed.lagFrames, observed.sampleRate) : juce::String();
    return false;
}

bool KirinHyphaEditor::refreshLiveCompare()
{
    processorRef.serviceLiveCompare();
    const auto status = processorRef.liveCompareStatus();
    if (liveBlindOpen) return false; // no identity-bearing notices or gain labels in Blind
    bool newFault = false;
    if (liveCompareFinishingSeen && ! status.finishing)
        showToast ("POST back to normal");
    liveCompareFinishingSeen = status.finishing;
    liveCompareMatched = status.matched;
    liveCompareLimited = status.matchLimited;
    const auto now = nowSecs();
    MatchPlan pendingPlan; bool applied = false;
    if (processorRef.takeLiveCompareMatch (pendingPlan, applied))
    {
        if (pendingPlan.failure != MatchFailure::none) showToast (matchFailure (pendingPlan.failure));
        else if (!applied) chooseLiveCompareMatch (pendingPlan);
        else
        {
            liveCompareAuto.approvedPreDb = pendingPlan.preGainDb;
            liveCompareAuto.ceilingDbtp = pendingPlan.ceilingDbtp;
            showToast ("MATCH: PRE " + signedDb (pendingPlan.preGainDb));
        }
    }
    processorRef.takeLiveComparePreWait(); // diagnostic receipt; short waits have no UI state
    liveComparePreWaitUntil = 0;
    const bool sessionObserved = status.active || liveCompareActiveSeen;
    const bool guardTripped = processorRef.takeLiveCompareGuardTrip();
    if (guardTripped && sessionObserved)
    {
        showToast (hypha::live_compare_ui::namedRecovery (status));
        newFault = true;
    }
    else if (status.interrupted && ! liveCompareInterruptSeen && sessionObserved)
    {
        showToast (hypha::live_compare_ui::namedRecovery (status));
        newFault = true;
    }
    liveCompareInterruptSeen = status.interrupted;
    // A format change, a changed pair or a closed PRE ended the session without END: say so.
    if (liveCompareActiveSeen && ! status.active && ! status.finishing)
    {
        showToast (hypha::live_compare_ui::namedRecovery (status));
        newFault = true;
    }
    liveCompareActiveSeen = status.active && ! status.finishing;
    newFault = monitorLiveCompareOffset (status, now) || newFault;
    // B, C or V in REF may hold the output; past 3 s LISTEN says so. The choice stays.
    const auto auditionSlot = processorRef.referenceOutputSlot();
    const auto presentation = hypha::live_compare_ui::currentListenPresentation (
        status, processorRef.liveCompareAdmission (false), auditionSlot);
    liveCompareStory.clear();
    if (*presentation.instruction != 0)
    {
        liveCompareWarning = presentation.instruction;
        const auto why = presentation.reason != hypha::live_compare::RecoveryReason::none ? presentation.reason
                                                                                         : status.observation;
        if (const auto* next = hypha::live_compare_ui::auditionHoldsListen (status, auditionSlot)
                ? hypha::live_compare_ui::auditionNextStep (auditionSlot, status.preSelected)
                : hypha::live_compare_ui::nextStep (presentation.action); *next != 0)
            liveCompareStory = { juce::String ("LISTEN: ") + hypha::live_compare_ui::cause (why)
                + (why == hypha::live_compare::RecoveryReason::cycleUnverified ? "" : "."), next };
    }
    if (newFault) liveCompareAuto.on = false;
    newFault = followLiveCompare (status, now) || newFault;
    hypha::observatory::LiveCompareFooter footer;
    footer.entryEnabled = processorRef.liveCompareSupported()
        && processorRef.liveCompareAdmission (false) == StartResult::started;
    footer.active = status.active;
    footer.finishing = status.finishing;
    footer.blindAvailable = processorRef.liveCompareAdmission (true) == StartResult::started;
    footer.preSelected = status.active && status.preSelected;
    footer.preWaiting = false; // selected PRE remains selected during quiet timing recovery
    footer.contentHeld = status.active && status.contentHeld;
    footer.compensationOff = status.active && status.compensationOff;
    footer.recoveryHelp = presentation.instruction;
    footer.matched = status.active && (liveCompareMatched || status.matchHeld);
    footer.matchHeld = false; // the approved MATCH stays visible across timing recovery
    footer.matchLimited = footer.matched && liveCompareLimited;
    footer.following = status.active && liveCompareAuto.on;
    footer.preGainTenthsDb = status.gain > 0.0f ? juce::roundToInt (200.0f * std::log10 (status.gain)) : 0;
    const auto held = std::min (status.postActual, status.postTarget);
    footer.postHeldTenthsDb = held > 0.0f && held < 1.0f
        ? juce::jmin (-1, juce::roundToInt (200.0f * std::log10 (held))) : referenceHeldTenthsDb();
    observatoryView.setLiveCompareFooter (footer);
    return newFault;
}

#endif
