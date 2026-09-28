#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>
#include <cstdlib>

// The editor side of the live PRE / POST compare (AGENTS R-12, INV-LC1 to INV-LC4). It starts and
// ends the session, forwards the PRE / POST choice and MATCH, and shows what the Audio Thread
// reports. Closing the editor ends the session, so a closed window never leaves PRE sounding.
namespace
{
using hypha::live_compare::MatchFailure;
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
        const auto result = processorRef.matchLiveCompare();
        if (! result.ok())
            showToast (matchFailure (result.failure));
        else
        {
            liveCompareMatched = true;
            liveCompareLimited = result.limitedByTruePeak;
            showToast (result.limitedByTruePeak
                ? "TP limit: PRE " + signedDb (result.appliedDb) + ", need " + signedDb (result.measuredDb)
                : "MATCH: PRE " + signedDb (result.appliedDb));
        }
        refreshLiveCompare();
    };
    observatoryView.onLiveCompareEnd = [this]
    {
        processorRef.stopLiveCompare();
        liveCompareMatched = false;
        liveCompareLimited = false;
        liveCompareActiveSeen = false;
        refreshLiveCompare();
    };
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
    if (status.interrupted && ! liveCompareInterruptSeen)
        showToast ("Select PRE again");
    liveCompareInterruptSeen = status.interrupted;
    // A format change, a changed pair or a closed PRE ended the session without END: say so.
    if (liveCompareActiveSeen && ! status.active)
        showToast ("LISTEN ended; POST plays");
    liveCompareActiveSeen = status.active;
    hypha::observatory::LiveCompareFooter footer;
    footer.entryEnabled = processorRef.liveCompareSupported();
    footer.active = status.active;
    footer.preSelected = status.active && status.preSelected;
    footer.preWaiting = footer.preSelected && (status.preWaiting || now < liveComparePreWaitUntil);
    footer.matched = status.active && liveCompareMatched;
    footer.matchLimited = footer.matched && liveCompareLimited;
    footer.preGainTenthsDb = status.gain > 0.0f ? juce::roundToInt (200.0f * std::log10 (status.gain)) : 0;
    observatoryView.setLiveCompareFooter (footer);
}

#endif
