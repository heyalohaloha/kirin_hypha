#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

#include "kirin_hypha_chain_observation.h"

namespace hypha::chain_action
{
inline juce::String tpThreshold (std::uint8_t severity)
{
    return severity == 3u ? ">0" : severity == 2u ? ">-1"
         : severity == 1u ? "<=-1" : "?";
}

inline juce::String summaryText (const KirinChainSnapshot& snapshot,
                                 const KirinChainPoint& point, bool compact)
{
    const auto relation = std::isfinite (point.relation)
        ? juce::String (point.relation >= 0.0 ? "+" : "") + juce::String (point.relation, 1)
        : juce::String ("?");
    const auto prefix = juce::String (snapshot.status == KIRIN_CHAIN_HOLD ? "HOLD " : "")
        + (compact ? "REL" : "REL ") + relation;
    if (! compact)
        return prefix + "  TP PRE" + tpThreshold (point.pre_severity)
            + " POST" + tpThreshold (point.post_severity);
    if (point.pre_severity == point.post_severity)
        return prefix + " BOTH" + tpThreshold (point.pre_severity);
    if (point.pre_severity == 1u && point.post_severity > 1u)
        return prefix + " POST" + tpThreshold (point.post_severity);
    if (point.post_severity == 1u && point.pre_severity > 1u)
        return prefix + " PRE" + tpThreshold (point.pre_severity);
    return prefix + " P" + tpThreshold (point.pre_severity)
        + " O" + tpThreshold (point.post_severity);
}
}
