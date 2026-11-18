#pragma once
#include "HyphaBlindPreferenceTrial.h"
#include "HyphaLanguage.h"
namespace hypha::blind_preference
{
inline juce::String summaryLine (const Summary& summary, bool candidate)
{
    if (summary.total() == 0) return {};
    if (summary.tied()) return i18n::tr ("%1 each.").replace ("%1", juce::String (summary.a));
    const auto name = summary.a > summary.b ? (candidate ? "A" : "PRE") : (candidate ? "B" : "POST");
    const auto count = summary.a > summary.b ? summary.a : summary.b;
    return i18n::tr ("You chose %1 in %2 of %3 rounds.").replace ("%1", name)
        .replace ("%2", juce::String (count)).replace ("%3", juce::String (summary.total()));
}
inline juce::String probabilityLine (const Summary& summary)
{
    if (!summary.chance.available) return {};
    if (summary.chance.lessThanOneInHundred())
        return i18n::tr ("Picking one side this often at random happens %1 times.")
            .replace ("%1", i18n::tr ("fewer than 1 in 100"));
    return i18n::tr ("Picking one side this often at random happens about %1 in 100 times.")
        .replace ("%1", juce::String (summary.chance.roundedPerHundred()));
}
inline juce::String historyText (const Trial& trial, bool candidate)
{
    juce::String result;
    for (unsigned i = 0; i < trial.answered(); ++i) {
        const auto* answer = trial.answerAt(i);
        if (!answer) continue;
        if (result.isNotEmpty()) result += "\n";
        result += i18n::tr ("Round %1: %2").replace ("%1", juce::String (i+1)).replace ("%2",
            answer->choseA ? (candidate ? "A" : "PRE") : (candidate ? "B" : "POST"));
    }
    return result;
}
}
