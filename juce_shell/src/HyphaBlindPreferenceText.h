#pragma once
#include "HyphaBlindPreferenceTrial.h"
#include "HyphaLanguage.h"
#include <cmath>
namespace hypha::blind_preference
{
inline juce::String sideName (bool sideA, bool candidate, const juce::String& nameA = {}, const juce::String& nameB = {}, bool image = false)
{
    const auto fallback = candidate ? (sideA ? "A" : "B") : (sideA ? "PRE" : "POST");
    const auto name = (sideA ? nameA : nameB).isEmpty() ? juce::String(fallback) : (sideA ? nameA : nameB);
    if (image && i18n::current() == i18n::Language::japanese && !candidate && name == fallback)
        return i18n::tr(sideA ? "Before processing (PRE)" : "After processing (POST)");
    return name;
}
inline juce::String roundProgress (unsigned round, unsigned total)
{
    return i18n::tr("Round %1 of %2").replace("%1",juce::String(round)).replace("%2",juce::String(total));
}
inline juce::String signedDb (double value)
{
    const auto rounded = std::round(value * 10.0) / 10.0;
    return (rounded < 0 ? juce::String::fromUTF8(u8"−") : juce::String("+")) + juce::String(std::abs(rounded),1);
}
inline juce::String summaryLine (const Summary& summary, bool candidate,
    const juce::String& nameA = {}, const juce::String& nameB = {}, bool image = false)
{
    if (summary.total() == 0) return {};
    if (summary.tied()) return i18n::tr ("%1 each.").replace ("%1", juce::String (summary.a));
    const auto name = sideName(summary.a > summary.b,candidate,nameA,nameB,image);
    const auto count = summary.a > summary.b ? summary.a : summary.b;
    return i18n::tr ("You chose %1 in %2 of %3 rounds.").replace ("%1", name)
        .replace ("%2", juce::String (count)).replace ("%3", juce::String (summary.total()));
}
inline juce::String probabilityLine (const Summary& summary)
{
    if (!summary.chance.available || summary.chance.roundedPerHundred() == 100) return {};
    if (summary.chance.lessThanOneInHundred())
        return i18n::tr ("Picking one side this often at random happens fewer than 1 in 100 times.");
    return i18n::tr ("Picking one side this often at random happens about %1 in 100 times.")
        .replace ("%1", juce::String (summary.chance.roundedPerHundred()));
}
}
