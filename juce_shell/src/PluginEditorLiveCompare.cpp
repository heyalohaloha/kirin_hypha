#include "PluginEditor.h"
#include "HyphaLiveCompareActionResult.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaLocalBlindAdmissionText.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>
#include <cstdlib>

// The editor side of the live PRE / POST compare (AGENTS R-12, INV-LC1 to INV-LC16). It starts and
// ends the session, forwards PRE / POST, MATCH and AUTO, asks before lowering POST, and shows RT
// receipts. Explicit END restores unity; unexpected window close holds attenuation until RETURN.
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

juce::String pinFailure (hypha::live_compare::PinFailure failure)
{
    using hypha::live_compare::PinFailure;
    switch (failure)
    {
        case PinFailure::none:        return {};
        case PinFailure::notProven:   return "PIN waits for PRE";
        case PinFailure::tooShort:    return "PIN needs 4 s of play";
        case PinFailure::notOneRange: return "Last 4 s not one range";
        case PinFailure::overwritten: return "PIN failed; try again";
    }
    return {};
}

juce::String startFailure (StartResult result)
{
    switch (result)
    {
        case StartResult::started:
        case StartResult::notPost:           return {};
        case StartResult::comparisonBusy:    return "End the current comparison first";
        case StartResult::returnPending:     return "Ended; normal level returns with audio";
        case StartResult::returnRequired:    return "Press RETURN first";
        case StartResult::notReady:          return "LISTEN could not start";
        case StartResult::noPair:            return "Choose the PRE first";
        case StartResult::unsupportedLayout: return "Mono / stereo only";
        case StartResult::preUnavailable:    return "Paired PRE unavailable";
        case StartResult::preMultiMono:      return "PRE is multi-mono: insert it as stereo";
    }
    return {};
}

juce::String matchFailure (MatchFailure failure)
{
    switch (failure)
    {
        case MatchFailure::none:            return {};
        case MatchFailure::notProven:       return "MATCH waits for PRE";
        case MatchFailure::tooShort:        return "MATCH needs 3 s of play";
        case MatchFailure::overwritten:     return "MATCH failed; try again";
        case MatchFailure::notEnoughSignal: return "MATCH needs more signal";
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
                showToast (result == StartResult::started ? "Closing returns to POST" : startFailure (result));
                if (result == StartResult::noPair) showCandidateMenu();
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
    observatoryView.onLiveComparePin = [this] { pinLiveCompareForBlind(); };
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
        refreshLiveCompare();
        showToast (matchFailure (result.failure));
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
    const auto generation = processorRef.liveCompareStatus().sessionGeneration;
    const auto run = processorRef.liveComparePlaybackRun();
    menu.showMenuAsync (options, [safe, plan, generation, run] (int result)
    {
        if (safe != nullptr && ! safe->liveBlindOpen && (result == 1 || result == 2)
            && safe->processorRef.liveCompareStatus().sessionGeneration == generation
            && safe->processorRef.liveComparePlaybackRun() == run)
            safe->applyLiveCompareChoice (plan, result == 1 ? MatchChoice::lowerPost : MatchChoice::limitPre);
    });
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

// INV-LC15: PIN fixes the last four seconds and opens PRE / POST Blind on them, past its capture
// step. The live session ends; Blind owns the output from here, and its own RETURN brings back POST.
void KirinHyphaEditor::pinLiveCompareForBlind()
{
    if (outputRefused (hypha::output_owner::Activity::localBlind))
        return;
    const auto result = processorRef.pinLiveCompareForBlind (processorRef.meterContextPreference());
    if (! result.pinned)
    {
        refreshLiveCompare();
        showToast (result.pin != hypha::live_compare::PinFailure::none
                       ? pinFailure (result.pin) : hypha::local_blind_ui::admissionText (result.admission));
        return;
    }
    processorRef.stopLiveCompare();
    liveCompareMatched = false;
    liveCompareLimited = false;
    liveCompareActiveSeen = false;
    liveCompareAuto = {};
    localBlindReturnIntent.clear();
    localBlindPreflight = false;
    localBlindView.setMeterContext (processorRef.meterContextPreference());
    localBlindOpen = true;
    localBlindView.clearActionNotice();
    refreshLocalBlindProduct();
    if (localBlindView.isShowing()) localBlindView.grabKeyboardFocus();
}

// Another audition must not start on top of an approved POST attenuation: RETURN first.
bool KirinHyphaEditor::monitorLiveCompareOffset (const hypha::live_compare::Status& status, double now)
{
    bool newFault = false;
    auto& m = liveCompareOffset;
    const auto run = processorRef.liveComparePlaybackRun();
    // INV-LC8: while the host's delay compensation is off, the content offset is the chain's
    // uncompensated latency, not a jump; the estimates start again once it is on.
    if (! status.active || run != m.run || status.compensationOff)
    {
        m = {};
        m.run = run;
    }
    if (status.active && ! status.compensationOff && status.verdict == hypha::live_compare::Verdict::accepted
        && now >= m.nextAt)
    {
        m.nextAt = now + 2.0;
        const auto estimate = processorRef.measureLiveCompareOffset();
        const auto step = m.monitor.observe (estimate, status.contentHeld);
        if (step.jumped)
        {
            processorRef.holdLiveCompareForContentJump (step.lagFrames);
            if (! liveBlindOpen) showToast ("Timing changed: stop/play DAW (POST)");
            newFault = ! liveBlindOpen;
        }
        if (step.settled)
        {
            m.lag = step.lagFrames;
            m.rate = estimate.sampleRate;
            m.warningUntil = std::llabs (m.lag) > 1 ? now + 10.0 : 0.0;
        }
    }
    liveCompareWarning = status.active && status.compensationOff ? juce::String ("Delay compensation is off in Pro Tools")
                       : status.active && now < m.warningUntil ? offsetText (m.lag, m.rate) : juce::String();
    return newFault;
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
    // Any waiting block since the last refresh reads, however short: WAIT stays for at least half
    // a second. The final minimum is a listening decision (plan G4).
    if (processorRef.takeLiveComparePreWait() || status.preWaiting)
        liveComparePreWaitUntil = now + 0.5;
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
    const auto presentation = hypha::live_compare_ui::currentNamedPresentation (status, processorRef.liveCompareAdmission (false));
    liveCompareStory.clear();
    if (*presentation.instruction != 0)
    {
        liveCompareWarning = presentation.instruction;
        const auto why = presentation.reason != hypha::live_compare::RecoveryReason::none ? presentation.reason
                                                                                         : status.observation;
        if (const auto* next = hypha::live_compare_ui::nextStep (presentation.action); *next != 0)
            liveCompareStory = { juce::String ("LISTEN: ") + hypha::live_compare_ui::cause (why) + ".", next };
    }
    if (newFault) liveCompareAuto.on = false;
    newFault = followLiveCompare (status, now) || newFault;
    hypha::observatory::LiveCompareFooter footer;
    footer.entryEnabled = processorRef.liveCompareSupported()
        && processorRef.liveCompareAdmission (false) == StartResult::started;
    footer.active = status.active;
    footer.finishing = status.finishing;
    footer.blindAvailable = status.matchReady
        && processorRef.liveCompareAdmission (true) == StartResult::started;
    footer.preSelected = status.active && status.preSelected;
    footer.preWaiting = footer.preSelected && (status.preWaiting || now < liveComparePreWaitUntil);
    footer.contentHeld = status.active && status.contentHeld;
    footer.compensationOff = status.active && status.compensationOff;
    footer.recoveryHelp = presentation.instruction;
    footer.pinAvailable = processorRef.localBlindProductSupported()
        && hypha::local_blind_ui::productEntryEnabled (processorRef.wrapperType);
    footer.matched = status.active && (liveCompareMatched || status.matchHeld);
    footer.matchHeld = status.active && status.matchHeld;
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
