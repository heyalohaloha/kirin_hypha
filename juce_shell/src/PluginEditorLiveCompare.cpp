#include "PluginEditor.h"
#include "HyphaLocalBlindAdmissionText.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>
#include <cstdlib>

// The editor side of the live PRE / POST compare (AGENTS R-12, INV-LC1 to INV-LC14). It starts and
// ends the session, forwards the PRE / POST choice, MATCH and RETURN, asks before lowering POST, and
// shows what the Audio Thread reports. Closing the editor ends the session, so a closed window
// never leaves PRE sounding; an approved POST attenuation stays until RETURN.
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
        case StartResult::notReady:          return "LISTEN could not start";
        case StartResult::noPair:            return "Choose the PRE first";
        case StartResult::unsupportedLayout: return "Mono / stereo only";
        case StartResult::preUnavailable:    return "Paired PRE unavailable";
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
    }
    return {};
}
}

void KirinHyphaEditor::configureLiveCompare()
{
    observatoryView.onLiveCompareStart = [this]
    {
        const auto result = processorRef.startLiveCompare();
        liveCompareMatched = false;
        liveCompareLimited = false;
        liveCompareInterruptSeen = false;
        if (result == StartResult::started)
            showToast ("Closing returns to POST");
        else
        {
            showToast (startFailure (result));
            if (result == StartResult::noPair)
                showCandidateMenu();
        }
        refreshLiveCompare();
    };
    observatoryView.onLiveCompareSelect = [this] (bool pre)
    {
        processorRef.selectLiveComparePre (pre);
        liveCompareInterruptSeen = false;
        refreshLiveCompare();
    };
    observatoryView.onLiveCompareMatch = [this]
    {
        const auto result = processorRef.measureLiveCompare();
        if (! result.ok())
        {
            showToast (matchFailure (result.failure));
            return;
        }
        const auto held = processorRef.liveCompareStatus().postTarget;
        const auto plan = hypha::live_compare::planMatch (result, held > 0.0f ? 20.0 * std::log10 (held) : 0.0);
        if (plan.needsApproval)
            chooseLiveCompareMatch (plan);
        else
            applyLiveCompareChoice (plan, MatchChoice::basis);
    };
    observatoryView.onLiveCompareEnd = [this]
    {
        processorRef.stopLiveCompare();
        liveCompareMatched = false;
        liveCompareLimited = false;
        liveCompareActiveSeen = false;
        refreshLiveCompare();
    };
    observatoryView.onLiveComparePin = [this] { pinLiveCompareForBlind(); };
    observatoryView.onLiveCompareReturn = [this]
    {
        processorRef.returnLiveComparePostToNormal();
        showToast ("POST back to normal");
        refreshLiveCompare();
    };
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
        if (safe != nullptr && (result == 1 || result == 2))
            safe->applyLiveCompareChoice (plan, result == 1 ? MatchChoice::lowerPost : MatchChoice::limitPre);
    });
}

void KirinHyphaEditor::applyLiveCompareChoice (const MatchPlan& plan, MatchChoice choice)
{
    if (! processorRef.applyLiveCompareMatch (plan, choice))
    {
        showToast ("MATCH failed; try again");
        return;
    }
    liveCompareMatched = true;
    liveCompareLimited = choice == MatchChoice::limitPre;
    showToast (choice == MatchChoice::lowerPost ? "MATCH: POST " + signedDb (plan.lowerPostGainDb)
               : choice == MatchChoice::limitPre
                   ? "TP limit: PRE " + signedDb (plan.limitedPreGainDb) + ", need " + signedDb (plan.neededPreGainDb)
                   : "MATCH: PRE " + signedDb (plan.preGainDb));
    refreshLiveCompare();
}

// INV-LC15: PIN fixes the last four seconds and opens PRE / POST Blind on them, past its capture
// step. The live session ends; Blind owns the output from here, and its own RETURN brings back POST.
void KirinHyphaEditor::pinLiveCompareForBlind()
{
    if (liveCompareHoldBlocksAudition())
        return;
    const auto result = processorRef.pinLiveCompareForBlind (processorRef.meterContextPreference());
    if (! result.pinned)
    {
        showToast (result.pin != hypha::live_compare::PinFailure::none
                       ? pinFailure (result.pin) : hypha::local_blind_ui::admissionText (result.admission));
        return;
    }
    processorRef.stopLiveCompare();
    liveCompareMatched = false;
    liveCompareLimited = false;
    liveCompareActiveSeen = false;
    localBlindReturnIntent.clear();
    localBlindPreflight = false;
    localBlindView.setMeterContext (processorRef.meterContextPreference());
    localBlindOpen = true;
    localBlindView.clearActionNotice();
    refreshLocalBlindProduct();
    localBlindView.grabKeyboardFocus();
}

// Another audition must not start on top of an approved POST attenuation: RETURN first.
bool KirinHyphaEditor::liveCompareHoldBlocksAudition()
{
    if (processorRef.liveCompareStatus().postTarget >= 1.0f)
        return false;
    showToast ("Press RETURN first");
    return true;
}

void KirinHyphaEditor::monitorLiveCompareOffset (const hypha::live_compare::Status& status, double now)
{
    auto& m = liveCompareOffset;
    const auto run = processorRef.liveComparePlaybackRun();
    if (! status.active || run != m.run)
    {
        m = {};
        m.run = run;
    }
    if (status.active && status.verdict == hypha::live_compare::Verdict::accepted && now >= m.nextAt)
    {
        m.nextAt = now + 2.0;
        const auto estimate = processorRef.measureLiveCompareOffset();
        const auto step = m.monitor.observe (estimate, status.contentHeld);
        if (step.jumped)
        {
            processorRef.holdLiveCompareForContentJump();
            showToast ("PRE held: latency changed");
        }
        if (step.settled)
        {
            m.lag = step.lagFrames;
            m.rate = estimate.sampleRate;
            m.warningUntil = std::llabs (m.lag) > 1 ? now + 10.0 : 0.0;
        }
    }
    liveCompareOffsetWarning = status.active && now < m.warningUntil
        ? offsetText (m.lag, m.rate) : juce::String();
}

void KirinHyphaEditor::refreshLiveCompare()
{
    processorRef.serviceLiveCompare();
    const auto status = processorRef.liveCompareStatus();
    const auto now = nowSecs();
    // Any waiting block since the last refresh reads, however short: WAIT stays for at least half
    // a second. The final minimum is a listening decision (plan G4).
    if (processorRef.takeLiveComparePreWait() || status.preWaiting)
        liveComparePreWaitUntil = now + 0.5;
    if (processorRef.takeLiveCompareGuardTrip())
        showToast ("PRE over TP ceiling");
    else if (status.interrupted && ! liveCompareInterruptSeen)
        showToast ("Select PRE again");
    liveCompareInterruptSeen = status.interrupted;
    // A format change, a changed pair or a closed PRE ended the session without END: say so.
    if (liveCompareActiveSeen && ! status.active)
        showToast ("LISTEN ended; POST plays");
    liveCompareActiveSeen = status.active;
    monitorLiveCompareOffset (status, now);
    hypha::observatory::LiveCompareFooter footer;
    footer.entryEnabled = processorRef.liveCompareSupported();
    footer.active = status.active;
    footer.preSelected = status.active && status.preSelected;
    footer.preWaiting = footer.preSelected && (status.preWaiting || now < liveComparePreWaitUntil);
    footer.contentHeld = status.active && status.contentHeld;
    footer.pinAvailable = processorRef.localBlindProductSupported()
        && hypha::local_blind_ui::productEntryEnabled (processorRef.wrapperType);
    footer.matched = status.active && liveCompareMatched;
    footer.matchLimited = footer.matched && liveCompareLimited;
    footer.preGainTenthsDb = status.gain > 0.0f ? juce::roundToInt (200.0f * std::log10 (status.gain)) : 0;
    footer.postHeldTenthsDb = status.postTarget > 0.0f && status.postTarget < 1.0f
        ? juce::jmin (-1, juce::roundToInt (200.0f * std::log10 (status.postTarget))) : 0;
    observatoryView.setLiveCompareFooter (footer);
}

#endif
