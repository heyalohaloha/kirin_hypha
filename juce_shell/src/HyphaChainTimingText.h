#pragma once

#include <juce_core/juce_core.h>

#include "live_compare/LiveCompareChainTiming.h"

// The words POST's information menu puts on a chain timing reading (LiveCompareChainTiming.h).
// A reading that was not measured is given its reason; it is never shown as a zero.
namespace hypha::chain_timing
{
inline juce::String reasonText (live_compare::ChainTimingReason reason)
{
    using Reason = live_compare::ChainTimingReason;
    switch (reason)
    {
        case Reason::noPre:         return "PRE timing is not available";
        case Reason::notPlaying:    return "playback is stopped";
        case Reason::preFeeding:    return "PRE is feeding PRE/POST LISTEN";
        case Reason::otherThread:   return "PRE and POST run on different threads";
        case Reason::unevenCalls:   return "PRE and POST are not called once each per block";
        case Reason::unevenBlocks:  return "PRE and POST get different block lengths";
        case Reason::orderUnproven: return "PRE is not confirmed to run before POST";
        case Reason::none:          break;
    }
    return {};
}

// A chain takes anything from microseconds to milliseconds: keep about three significant places.
inline juce::String number (double value)
{
    return juce::String (value, value >= 10.0 ? 1 : value >= 0.1 ? 2 : 3);
}

// The footer's one-line form: the typical / peak share of the block's own length, as a load.
// Milliseconds next to PRE and POST would read as an offset between them, so the footer leaves
// them to the information menu. A reading that was not measured is shown as dashes; its reason is
// in the menu. caution marks a peak block that took longer than its own length, the case in which
// the host cannot keep up.
struct FooterReadout
{
    juce::String text;
    bool caution = false;
};

inline juce::String wholePercent (double share)
{
    const auto percent = share * 100.0;
    return percent < 1.0 ? juce::String ("<1%") : juce::String (juce::roundToInt (percent)) + "%";
}

inline FooterReadout footerReadout (const live_compare::ChainTimingView& view)
{
    if (view.state != live_compare::ChainTimingView::State::measuring)
        return { "CHAIN LOAD --", false };
    return { "CHAIN LOAD " + wholePercent (view.typicalLoad) + " / " + wholePercent (view.peakLoad),
             view.peakLoad >= 1.0 };
}

inline juce::StringArray lines (const live_compare::ChainTimingView& view)
{
    using State = live_compare::ChainTimingView::State;
    juce::StringArray result;
    if (view.state == State::waiting)
        result.add ("Waiting for audio callbacks");
    else if (view.state == State::unavailable)
        result.add ("Not measured: " + reasonText (view.reason));
    else
    {
        result.add ("Elapsed " + number (view.typicalMs) + " ms typical / "
                    + number (view.peakMs) + " ms peak");
        result.add ("Share of a " + number (view.blockMs) + " ms block: "
                    + number (view.typicalLoad * 100.0) + "% typical / "
                    + number (view.peakLoad * 100.0) + "% peak");
        if (view.countedShare < 0.995 && view.reason != live_compare::ChainTimingReason::none)
            result.add ("Counted " + number (view.countedShare * 100.0) + "% of blocks ("
                        + reasonText (view.reason) + ")");
    }
    return result;
}
}
