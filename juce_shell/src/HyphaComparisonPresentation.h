#pragma once

#include <juce_core/juce_core.h>

#include "kirin_hypha_ffi.h"

namespace hypha::comparison_presentation
{
inline juce::String statusText (uint8_t state, uint8_t reason)
{
    switch (reason)
    {
        case KIRIN_COMPARISON_REASON_NONE: return {};
        case KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT:
            return juce::CharPointer_UTF8 ("MATCHED PAIR — MEASURING");
        case KIRIN_COMPARISON_REASON_STALE:
            return state == KIRIN_COMPARISON_STATE_HOLDING
                ? juce::CharPointer_UTF8 ("PRE UPDATE DELAYED — HOLDING MATCHED Δ")
                : juce::CharPointer_UTF8 ("PRE UPDATE DELAYED — WAITING FOR MATCHED DATA");
        case KIRIN_COMPARISON_REASON_PRE_BYPASSED:
            return juce::CharPointer_UTF8 ("PRE IS OFF — ENABLE PRE TO COMPARE");
        case KIRIN_COMPARISON_REASON_PRE_INACTIVE:
            return juce::CharPointer_UTF8 ("PRE INACTIVE — START PLAYBACK");
        case KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH:
            return juce::CharPointer_UTF8 ("CHANNEL LAYOUTS DIFFER — MATCH PRE / POST BUS");
        case KIRIN_COMPARISON_REASON_LAYOUT_UNKNOWN:
            return juce::CharPointer_UTF8 ("PRE LAYOUT UNKNOWN — UPDATE OR REOPEN PRE");
        case KIRIN_COMPARISON_REASON_AUDITION_ACTIVE:
            return juce::CharPointer_UTF8 ("REFERENCE AUDITION — RETURN TO A TO COMPARE");
        case KIRIN_COMPARISON_REASON_UNSUPPORTED_VIEW:
            return juce::CharPointer_UTF8 ("VIEW CANNOT BE COMPARED — CHOOSE A SUPPORTED VIEW");
        case KIRIN_COMPARISON_REASON_UNSUPPORTED_METRIC:
            return juce::CharPointer_UTF8 ("METRIC CANNOT BE COMPARED — CHOOSE A SUPPORTED METRIC");
        case KIRIN_COMPARISON_REASON_NO_PAIR:
        default:
            return juce::CharPointer_UTF8 ("NO MATCHING PRE — SELECT A PRE");
    }
}

inline bool notifiesExplicitAction (uint8_t state, uint8_t reason) noexcept
{
    if (state != KIRIN_COMPARISON_STATE_REJECTED
        && state != KIRIN_COMPARISON_STATE_POST_ABSOLUTE)
        return false;
    return reason != KIRIN_COMPARISON_REASON_NONE
        && reason != KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT
        && reason != KIRIN_COMPARISON_REASON_STALE;
}
}
