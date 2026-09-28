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
        case StartResult::notReady:          return "PRE / POST listening could not start. Try again";
        case StartResult::noPair:            return "Choose the PRE for this POST first";
        case StartResult::unsupportedLayout: return "PRE / POST listening is for mono / stereo only";
        case StartResult::preUnavailable:    return "The paired PRE is not available. Check that it is active";
    }
    return {};
}

juce::String matchFailure (MatchFailure failure)
{
    switch (failure)
    {
        case MatchFailure::none:            return {};
        case MatchFailure::notProven:       return "MATCH waits for PRE. Play the song";
        case MatchFailure::tooShort:        return "MATCH needs three seconds of continuous playback";
        case MatchFailure::overwritten:     return "MATCH could not read a stable range. Try again";
        case MatchFailure::notEnoughSignal: return "MATCH needs more signal in the latest four seconds";
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
        liveCompareInterruptSeen = false;
        if (result == StartResult::started)
            showToast ("PRE / POST listening started. Closing this window returns to POST");
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
            showToast (result.limitedByTruePeak
                ? "PRE gain limited to " + signedDb (result.appliedDb) + " by the True Peak ceiling; "
                      + signedDb (result.measuredDb) + " would match"
                : "PRE matched to POST: " + signedDb (result.appliedDb) + " over "
                      + juce::String (result.seconds, 1) + " s");
        }
        refreshLiveCompare();
    };
    observatoryView.onLiveCompareEnd = [this]
    {
        processorRef.stopLiveCompare();
        liveCompareMatched = false;
        liveCompareActiveSeen = false;
        refreshLiveCompare();
    };
}

void KirinHyphaEditor::refreshLiveCompare()
{
    processorRef.serviceLiveCompare();
    const auto status = processorRef.liveCompareStatus();
    const auto now = nowSecs();
    // A wait shorter than one refresh still reads: WAIT stays for at least half a second. The
    // final minimum is a listening decision (plan G4).
    if (status.preWaiting)
        liveComparePreWaitUntil = now + 0.5;
    if (status.interrupted && ! liveCompareInterruptSeen)
        showToast ("PRE was deselected. Select PRE again");
    liveCompareInterruptSeen = status.interrupted;
    // A format change, a changed pair or a closed PRE ended the session without END: say so.
    if (liveCompareActiveSeen && ! status.active)
        showToast ("PRE / POST listening ended. POST is playing");
    liveCompareActiveSeen = status.active;
    hypha::observatory::LiveCompareFooter footer;
    footer.entryEnabled = processorRef.liveCompareSupported();
    footer.active = status.active;
    footer.preSelected = status.active && status.preSelected;
    footer.preWaiting = footer.preSelected && (status.preWaiting || now < liveComparePreWaitUntil);
    footer.matched = status.active && liveCompareMatched;
    footer.preGainTenthsDb = status.gain > 0.0f ? juce::roundToInt (200.0f * std::log10 (status.gain)) : 0;
    observatoryView.setLiveCompareFooter (footer);
}

#endif
