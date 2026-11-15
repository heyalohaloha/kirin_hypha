#include "PluginEditor.h"
#include "HyphaQueuedStartText.h"
#include "HyphaKeepPreWait.h"
#include "HyphaLiveCompareActionResult.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaReferenceAuditionRecoveryText.h"
#include "HyphaLocalBlindAdmissionText.h"
#include "HyphaLiveStartAfterReturn.h"
#include "HyphaPairLabels.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include <cmath>
#include <cstdlib>
// The editor side of the live PRE / POST compare (AGENTS R-12, INV-LC1 to INV-LC16). It starts and
// ends the session, forwards PRE / POST, MATCH and AUTO, asks before lowering POST, and shows RT
// receipts. Explicit END restores unity; window closure keeps the processor-owned comparison.
namespace
{
using hypha::live_compare::MatchChoice;
using hypha::live_compare::MatchFailure;
using hypha::live_compare::MatchPlan;
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
juce::String offsetText (std::int64_t lag, double sampleRate, bool candidate)
{
    if (sampleRate <= 0.0)
        return {};
    const auto ms = std::fabs (static_cast<double> (lag)) * 1000.0 / sampleRate;
    return juce::String (candidate ? "A " : "PRE ") + juce::String (ms, 2) + (lag < 0 ? " ms early" : " ms late");
}
// INV-LC28: the amount the audition's read position already takes up.
juce::String alignedText (double ms, bool candidate)
{
    return juce::String (candidate ? "A " : "PRE ") + juce::String (ms, 2) + " ms aligned";
}
juce::String matchFailure (MatchFailure failure)
{
    switch (failure)
    {
        case MatchFailure::none:            return {};
        // These only end a MATCH at its deadline (it measures again until then); LISTEN goes on.
        case MatchFailure::notProven:       return "No MATCH: timing not confirmed";
        case MatchFailure::tooShort:
        case MatchFailure::overwritten:
        case MatchFailure::notEnoughSignal: return "No MATCH: not enough signal";
        case MatchFailure::outOfRange:      return "MATCH over 24 dB";
        case MatchFailure::stale:
        case MatchFailure::invalidPlan:     return "MATCH failed; try again";
    }
    return {};
}
}
void KirinHyphaEditor::configureLiveCompare()
{
    observatoryView.onCandidateCompare = [this] { showCandidateCompareMenu(); };
    observatoryView.onLiveCompareStart = [this] { requestListenStart (); };
    observatoryView.onLiveCompareSelect = [this] (bool pre) { requestListenSelect (pre); };
    // INV-LC16: a window that shows the same LISTEN again continues the AUTO that rested. Loop wraps and
    // playback gaps move sessionGeneration, not the LISTEN; a rest that cannot continue says so.
    const auto rest = std::exchange (processorRef.liveCompareAutoRest(), {});
    const auto current = processorRef.liveCompareStatus();
    if (isPost && (rest.on || rest.matchKnown) && current.active && ! current.finishing
        && rest.session == processorRef.liveCompareListenSession() && rest.ticket == processorRef.restoreOrPairTicket()
        && (current.matched || current.matchHeld || current.matchOff) && ! current.matchLimited)
    {
        liveCompareAuto = { rest.on, false, rest.approvedPreDb, rest.ceilingDbtp,
                            nowSecs() + hypha::live_compare::followIntervalSeconds, rest.matchKnown };
        liveCompareAuto.peakDbtp = rest.peakDbtp;
    }
    else if (isPost && rest.on)
        liveCompareAuto.hiddenStopNoticePending = true;
    // Once matched, MATCH offers MATCH again and AUTO (INV-LC16); the first MATCH measures at once.
    observatoryView.onLiveCompareMatch = [this] { requestListenMatch (); };
    observatoryView.onLiveCompareEnd = [this] { requestListenEnd (); };
    observatoryView.onLiveCompareReturn = [this] { requestNormalLevelReturn (); };
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
    const bool candidate = processorRef.candidateCompareActive();
    const auto heading = juce::String (candidate ? "A needs " : "PRE needs ") + signedDb (plan.neededPreGainDb)
        + "; TP ceiling allows " + signedDb (plan.limitedPreGainDb);
    const auto lower = juce::String (candidate ? "Lower B by " : "Lower POST by ")
        + magnitudeDb (plan.lowerPostGainDb) + (candidate ? "; A stays at its level" : "; PRE stays at its level");
    const auto limit = juce::String (candidate ? "Raise A by " : "Raise PRE by ") + signedDb (plan.limitedPreGainDb) + " only (TP LIMIT)";
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    auto& matchAnchor = observatoryView.liveMatchAnchor();
    auto& anchor = matchAnchor.isVisible() ? matchAnchor : observatoryView.operationsMenuAnchor();
    matchChoicePopup.open (scaleRoot, scaleRoot.getLocalArea (&anchor, anchor.getLocalBounds()), heading, lower, limit,
        logicalPresentationContext(), [safe, plan] (int result)
        { if (safe != nullptr) safe->handleLiveCompareMatchChoice (plan, result); });
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
            liveCompareAuto.limit = hypha::live_compare::FollowLimit::none;
            liveCompareAuto.peakDbtp = plan.prePeakDbtp;
            liveCompareAuto.matchKnown = true;
            liveCompareAuto.nextAt = nowSecs() + 1.0;
            liveCompareAuto.on = liveCompareAuto.on && ! liveCompareLimited;
            return hypha::live_compare_ui::ActionOutcome::success;
        }, [this] { return refreshLiveCompare(); },
        [this, &generation]
        { return hypha::live_compare_ui::currentNamedAction (processorRef.liveCompareStatus(), generation, true); },
        [this, &plan, choice, &applied]
        {
            showToast (! applied ? matchFailure (applied.failure)
                : choice == MatchChoice::lowerPost ? juce::String (processorRef.candidateCompareActive() ? "MATCH: B " : "MATCH: POST ") + signedDb (plan.lowerPostGainDb)
                : choice == MatchChoice::limitPre
                    ? juce::String (processorRef.candidateCompareActive() ? "TP limit: A " : "TP limit: PRE ") + signedDb (plan.limitedPreGainDb) + ", need " + signedDb (plan.neededPreGainDb)
                    : juce::String (processorRef.candidateCompareActive() ? "MATCH: A " : "MATCH: PRE ") + signedDb (plan.preGainDb));
        });
}

// Another audition must not start on top of an approved POST attenuation: RETURN first.
bool KirinHyphaEditor::monitorLiveCompareOffset (const hypha::live_compare::Status& status, double now)
{
    juce::ignoreUnused (now);
    const auto observed = processorRef.liveContentTimingObservation();
    const auto alignedMs = processorRef.liveCompareAlignMs();
    // INV-LC10: past 3 s a strong estimate the clock cannot act on is told with its amount; PRE plays.
    const auto notice = offsetText (status.contentJumpLagFrames, observed.sampleRate, processorRef.candidateCompareActive());
    liveCompareWarning = status.active && status.prolongedWait && status.contentHeld
        ? (notice.isEmpty() ? juce::String() : notice + " / ") + hypha::live_compare_ui::chainDelayNotice()
        : status.active && status.contentNotRemeasured ? hypha::live_compare_ui::notRemeasuredNotice()
        : status.active && processorRef.candidateAlignUnconfirmed() ? hypha::live_compare_ui::candidateUnconfirmedNotice()
        : status.active && status.verdict == hypha::live_compare::Verdict::accepted
            && processorRef.liveCompareAlignPending() ? (processorRef.candidateCompareActive() ? "Aligning A" : "Aligning PRE")
        : status.active && status.contentUnmeasured
            ? juce::String (hypha::live_compare_ui::unmeasuredNotice()) + " / " + hypha::live_compare_ui::chainDelayNotice()
        : status.active && observed.verifySamples ? "Repeated audio: checked per block"
        : status.active && alignedMs > 0.0 ? alignedText (alignedMs, processorRef.candidateCompareActive())
        : status.active && observed.settled && std::llabs (observed.lagFrames) > 1
            ? offsetText (observed.lagFrames, observed.sampleRate, processorRef.candidateCompareActive()) : juce::String();
    return false;
}

bool KirinHyphaEditor::refreshLiveCompare()
{
    processorRef.serviceLiveCompare();
    const auto status = processorRef.liveCompareStatus();
    if (! status.active || status.finishing || liveBlindOpen) matchChoicePopup.close();
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
            liveCompareAuto.limit = hypha::live_compare::FollowLimit::none;
            liveCompareAuto.peakDbtp = pendingPlan.prePeakDbtp;
            liveCompareAuto.matchKnown = true;
            showToast (juce::String (processorRef.candidateCompareActive() ? "MATCH: A " : "MATCH: PRE ") + signedDb (pendingPlan.preGainDb));
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
    const auto presentation = hypha::live_compare_ui::candidateNamedPresentation (status,
        hypha::live_compare_ui::currentListenPresentation (status, processorRef.liveCompareAdmission (false), auditionSlot),
        processorRef.candidateCompareActive(), processorRef.candidateHostInactive(), processorRef.candidateBypassed());
    liveCompareStory.clear();
    if (*presentation.instruction != 0)
    {
        if (! status.contentHeld || juce::String (presentation.instruction) != hypha::live_compare_ui::chainDelayNotice())
            liveCompareWarning = presentation.instruction;
        const auto why = presentation.reason != hypha::live_compare::RecoveryReason::none ? presentation.reason
                                                                                         : status.observation;
        if (const auto* next = hypha::live_compare_ui::auditionHoldsListen (status, auditionSlot)
                ? hypha::live_compare_ui::auditionNextStep (auditionSlot, status.preSelected)
                : hypha::live_compare_ui::nextStep (presentation.action); *next != 0)
            liveCompareStory = { juce::String ("LISTEN: ") + hypha::live_compare_ui::cause (why)
                + (why == hypha::live_compare::RecoveryReason::cycleUnverified ? "" : "."), next };
    }
    if (processorRef.candidateCompareActive() && juce::String (presentation.instruction)
        == hypha::live_compare_ui::candidateRecoveryText (processorRef.candidateHostInactive(), processorRef.candidateBypassed()))
    {
        const auto candidates = processorRef.enumeratePreCandidatesForMenu();
        juce::String first, second;
        for (const auto& candidate : candidates)
        {
            auto label = hypha::pair_labels::menuLabel (candidates, candidate);
            const auto id = candidate.instanceId.substring (0, 8);
            if (!label.contains (id)) label += " / " + id;
            if (candidate.instanceId == processorRef.pairedPreInstanceId()) first = label;
            if (candidate.instanceId == processorRef.liveComparePreInstance()) second = label;
        }
        liveCompareStory = { liveCompareWarning, "PRE1: " + first + " / PRE2: " + second };
    }
    if (processorRef.candidateCompareActive() && liveCompareWarning.isEmpty()
        && ! liveCompareMatched && processorRef.candidateMatchSecondsRemaining() > 0)
        liveCompareWarning = "MATCH: " + juce::String (processorRef.candidateMatchSecondsRemaining()) + " s remaining";
    if (newFault) liveCompareAuto.on = false;
    followLiveCompare (status, now);
    if (liveCompareWarning.isEmpty() && liveCompareAuto.on && ! status.matchOff)
        liveCompareWarning = juce::String (hypha::live_compare_ui::autoLimitNotice (liveCompareAuto.limit, liveCompareAuto.shortDb, processorRef.candidateCompareActive()));
    // A press waiting for its start says what it waits for, every refresh until it starts or drops.
    if ((liveCompareWarning.isEmpty() || status.finishing) && liveCompareStartAfterReturnUntil > 0.0)
    {
        using hypha::live_compare_ui::QueuedStart;
        const auto start = liveCompareStartAfterReturnCandidate.isNotEmpty() ? QueuedStart::candidate
                         : liveCompareStartAfterReturnBlind ? QueuedStart::blind : QueuedStart::listen;
        liveCompareWarning = hypha::live_compare_ui::queuedStartNotice (start, hypha::live_compare_ui::queuedWait (
            processorRef.isPlaying(), status.finishing, processorRef.candidateReturning()));
    }
    if ((liveCompareWarning.isEmpty() || status.finishing) && afterTail.action != hypha::live_compare_ui::AfterTail::none)
        liveCompareWarning = hypha::live_compare_ui::afterTailNotice (afterTail.action, processorRef.isPlaying());
    hypha::observatory::LiveCompareFooter footer;
    footer.entryEnabled = processorRef.liveCompareSupported()
        && hypha::live_compare_ui::entryOffered (processorRef.liveCompareAdmission (false));
    footer.active = status.active;
    footer.candidate = processorRef.candidateCompareActive();
    footer.finishing = status.finishing && ! processorRef.candidateReturning();
    footer.blindAvailable = hypha::live_compare_ui::entryOffered (processorRef.liveCompareAdmission (true));
    footer.entrySupported = processorRef.liveCompareSupported();
    footer.entryRefusal = liveCompareEntryRefusal (false);
    footer.blindRefusal = liveCompareEntryRefusal (true);
    footer.preSelected = status.active && status.preSelected;
    footer.preWaiting = false; // selected PRE remains selected during quiet timing recovery
    footer.contentHeld = status.active && status.contentHeld; // INV-LC10 notice; PRE still sounds
    footer.compensationOff = status.active && status.compensationOff;
    footer.recoveryHelp = presentation.instruction;
    footer.matched = status.active && (liveCompareMatched || status.matchHeld);
    footer.matchHeld = false; // the approved MATCH stays visible across timing recovery
    footer.matchLimited = footer.matched && liveCompareLimited;
    footer.matchOff = status.active && status.matchOff;
    footer.following = status.active && liveCompareAuto.on && ! status.matchOff; // AUTO rests while MATCH is off
    footer.preGainTenthsDb = status.gain > 0.0f ? juce::roundToInt (200.0f * std::log10 (status.gain)) : 0;
    const auto held = std::min (status.postActual, status.postTarget);
    footer.postHeldTenthsDb = held > 0.0f && held < 1.0f
        ? juce::jmin (-1, juce::roundToInt (200.0f * std::log10 (held))) : referenceHeldTenthsDb();
    observatoryView.setLiveCompareFooter (footer);
    return newFault;
}


// INV-LC14 (2026-10-31): the LISTEN or BLIND press waits while POST returns to normal and starts once it is back.
void KirinHyphaEditor::serviceLiveCompareStartAfterReturn()
{
    if (liveCompareStartAfterReturnUntil <= 0.0) return;
    // Target and other activities first; only then may the press start LISTEN. The role its own
    // RETURN still fades out is not another comparison (the audio start guard is unchanged).
    using hypha::live_compare_ui::StartAfterReturn;
    const bool blind = liveCompareStartAfterReturnBlind;
    const auto admission = processorRef.liveCompareAdmission (blind);
    auto reference = processorRef.referenceOwnOutputStates();
    // The ended candidate BLIND still owns its capture scope while B's history drains.
    // That same scope is not a new audition that cancels the queued press.
    if (processorRef.liveCompareReturningScope() != 0) reference &= ~hypha::output_owner::bit (hypha::output_owner::State::localBlind);
    const auto next = hypha::live_compare_ui::whileReturning (admission, reference);
    if (waitingPairKey() != liveCompareStartAfterReturnKey || processorRef.liveCompareStatus().active
        || next == StartAfterReturn::drop || localBlindOpen || liveBlindOpen)
    {
        liveCompareStartAfterReturnCandidate.clear();
        liveCompareStartAfterReturnUntil = 0.0;  // PRE, state or another comparison changed
        return;
    }
    // The return has its own overall deadline (an A/B tail 10 s after END; POST's level within 0.5 s
    // while stopped), so waiting for it never moves this press's 10 s. A press that can start starts,
    // even just past those 10 s; only a press still refused for another reason ends with the reason.
    // Bypass and offline rendering can keep callbacks coming without allowing the return to complete.
    // Past the press's deadline, explain the hold and end the press; the RETURN itself continues.
    if (processorRef.candidateReturning() || processorRef.liveCompareStatus().finishing)
    {
        const auto* bypass = processorRef.getBypassParameter();
        const bool bypassed = bypass != nullptr && bypass->getValue() >= 0.5f;
        const bool offline = processorRef.isNonRealtime();
        if (nowSecs() < liveCompareStartAfterReturnUntil || (! bypassed && ! offline)) return;
        liveCompareStartAfterReturnUntil = 0.0;
        liveCompareStartAfterReturnCandidate.clear();
        showToast (offline ? hypha::live_compare_ui::offlineHoldsReturnNotice : hypha::live_compare_ui::bypassHoldsReturnNotice);
        return;
    }
    if (next != StartAfterReturn::start)
    {
        if (nowSecs() < liveCompareStartAfterReturnUntil) return;
        liveCompareStartAfterReturnUntil = 0.0;
        liveCompareStartAfterReturnCandidate.clear();
        showToast (hypha::live_compare_ui::startFailure (admission));
        return;
    }
    liveCompareStartAfterReturnUntil = 0.0;
    const auto pre2 = std::exchange (liveCompareStartAfterReturnCandidate, {});
    if (pre2.isNotEmpty())
    {
        startCandidateConfirmed (pre2, liveCompareStartAfterReturnTicket, false);
        return;
    }
    if (blind)
    {
        openLiveBlind (liveCompareStartAfterReturnTicket);
        return;
    }
    liveCompareStartPressed = liveCompareStartAfterReturnTicket;  // the processor checks it under the pair lock
    if (observatoryView.onLiveCompareStart) observatoryView.onLiveCompareStart();
    liveCompareStartPressed.reset();
}

// After END of A/B, POST first returns to its level, then B plays out what it was fed during A/B, and the
// output table keeps V, C, B and Keep off until both are done (the 4 s of audio, CandidateProcessorControl).
// A press in that time is not refused: it waits and starts by itself once they are done, and the status
// line says so (2026-11-11). A later press, A in REF, leaving REF or another REF selection, another pair or
// restore, a new A/B, a LISTEN or Blind started meanwhile, or a hidden window drops it.
bool KirinHyphaEditor::waitsForAbTail (hypha::output_owner::Activity activity, hypha::live_compare_ui::AfterTail action)
{
    using hypha::output_owner::State;
    const auto decision = processorRef.outputDecision (activity);
    // The blockers of this same return: POST returning, a candidate BLIND scope draining, B's tail.
    const bool sameReturn = decision.cause == State::candidateSession || decision.cause == State::liveFinishing
                         || decision.cause == State::liveBlind;
    // INV-AB6: the return ended at its deadline alone; Keep also waits while B may still play the comparison.
    const bool unprovenTail = activity == hypha::output_owner::Activity::recordKeep && ! decision.refused()
                           && processorRef.candidateTailPending();
    if (! unprovenTail && (! decision.refused() || ! sameReturn || ! processorRef.candidateReturning()
        || processorRef.candidateCompareActive()))
        return false;
    const auto pair = processorRef.pairWaitSnapshot();
    if (! pair.stable) { if (action == hypha::live_compare_ui::AfterTail::keepAll) showToast ("All Keep targets changed or unavailable. Try again."); return true; }
    if (action == hypha::live_compare_ui::AfterTail::keepAll)
    {
        if (processorRef.keepAll (pair.ticket)) return true;
        const auto notice = processorRef.drainKeepActionNotice();
        if (notice != "A/B tail pending")
        { showToast (notice.isNotEmpty() ? notice : "All Keep targets changed or unavailable. Try again."); return true; }
    }
    // One waiting press at a time: this one replaces a waiting LISTEN, BLIND, A/B or C MATCH.
    liveCompareStartAfterReturnUntil = referenceRematchUntil = 0.0;
    liveCompareStartAfterReturnCandidate.clear();
    afterTail = { action, pair.key, pair.ticket, nowSecs(), 0.0,
                  action == hypha::live_compare_ui::AfterTail::keepAll };
    return true;  // the status line names the wait from the next refresh (no toast to outlive it)
}
void KirinHyphaEditor::dropReferenceAfterTail() noexcept
{
    using hypha::live_compare_ui::AfterTail;
    if (afterTail.action == AfterTail::referenceV || afterTail.action == AfterTail::referenceC
        || afterTail.action == AfterTail::referenceB)
        afterTail = {};
}
void KirinHyphaEditor::serviceAfterTailPress()
{
    using hypha::live_compare_ui::AfterTail;
    if (afterTail.action == AfterTail::none) return;
    const bool reference = afterTail.action == AfterTail::referenceV || afterTail.action == AfterTail::referenceC
                        || afterTail.action == AfterTail::referenceB;
    const auto pair = processorRef.pairWaitSnapshot();
    const bool restoringGroup = afterTail.action == AfterTail::keepAll && afterTail.groupStaged && ! pair.paired;
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (hypha::live_compare_ui::afterGroupRestoreReadForTest) hypha::live_compare_ui::afterGroupRestoreReadForTest();
   #endif
    if (! pair.stable || (!restoringGroup && pair.key != afterTail.pairKey) || pair.ticket != afterTail.ticket
        || processorRef.candidateCompareActive() || processorRef.liveCompareStatus().active
        || liveBlindOpen || localBlindOpen
        || (reference && observatoryDomain != hypha::observatory::Domain::reference))
    {
        const bool all = afterTail.action == AfterTail::keepAll;
        afterTail = {};
        if (all) showToast ("All Keep targets changed or unavailable. Try again.");
        return;
    }
    if (!reference && nowSecs() - afterTail.since >= hypha::keep_ui::preWaitSeconds)
    { afterTail = {}; showToast (hypha::live_compare_ui::keepTailEndedNotice); return; }
    if (restoringGroup) return;
    if (processorRef.candidateReturning() || processorRef.liveCompareStatus().finishing)
    {
        const auto* bypass = processorRef.getBypassParameter();
        const bool bypassed = bypass != nullptr && bypass->getValue() >= 0.5f;
        const bool offline = processorRef.isNonRealtime();
        if (nowSecs() - afterTail.since >= 10.0 && (bypassed || offline))
        { afterTail = {}; showToast (offline ? hypha::live_compare_ui::offlineHoldsReturnNotice : hypha::live_compare_ui::bypassHoldsReturnNotice); }
        return;
    }
    if (afterTail.action == AfterTail::keepAll && nowSecs() < afterTail.nextGroupCheck) return;
    if (! reference && (processorRef.candidateTailPending()
        || (afterTail.action == AfterTail::keepAll && processorRef.allKeepTailPending())))
    {
        afterTail.nextGroupCheck = nowSecs() + hypha::keep_ui::preRetrySeconds;
        return;
    }
    const auto waited = std::exchange (afterTail, {});
    switch (waited.action)
    {
        case AfterTail::referenceV: if (referenceView.onSelectB) referenceView.onSelectB(); break;
        case AfterTail::referenceC: if (referenceView.onSelectC) referenceView.onSelectC(); break;
        case AfterTail::referenceB: if (referenceView.onSelectRef) referenceView.onSelectRef(); break;
        case AfterTail::keepPair:   requestKeepPair (false, waited.ticket); break;
        case AfterTail::keepAll:    requestKeepAll (waited.ticket, waited.groupStaged, waited.since); break;
        case AfterTail::none:       break;
    }
}
#endif

#include "CandidateCompareMenu.h"
