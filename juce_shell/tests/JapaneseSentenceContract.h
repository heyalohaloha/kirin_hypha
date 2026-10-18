#pragma once

#include "../src/HyphaLanguage.h"
#include "../src/HyphaLevelMetricContract.h"

#include <cstdlib>
#include <iostream>

// A help built from several catalog sentences reads in Japanese sentence by sentence (INV-S40).
// LEVEL's value help is "PRE. " + what the value is + the Session note while the Session summary
// is pending; the whole was not one catalog entry, so the value read in English after "PREの".
namespace hypha::tests
{
inline void verifyJapaneseSentences()
{
    const auto require = [] (bool valid, const char* message)
    {
        if (valid) return;
        std::cerr << "Japanese sentences: " << message << '\n';
        std::exit (EXIT_FAILURE);
    };
    const auto japanese = [] (const juce::String& english)
    { return i18n::translate (english, i18n::Language::japanese); };
    using Metric = level_metrics::Metric;
    const juce::String notes[] {
        "Latest Session input coverage is not confirmed; MAX TP is a confirmed lower bound and PLR is unavailable.",
        "Session values cover processed audio. The final analysis chunk is pending; MAX TP is a confirmed lower bound and PLR is unavailable."
    };
    for (const auto* side : { "PRE. ", "POST. ", "POST minus PRE. " })
        for (const auto metric : { Metric::integrated, Metric::maximumTruePeak, Metric::loudnessRange, Metric::plr })
        {
            const juce::String help (level_metrics::scopeHelp (metric));
            for (const auto& note : notes)
            {
                const auto shown = japanese (side + help + " " + note);
                require (shown == japanese (side + help) + japanese (note),
                         "the value help and the Session note each read in Japanese");
                require (! shown.contains (help.substring (0, 12)) && ! shown.contains ("Session values"),
                         "no English sentence is left inside the Japanese help");
            }
        }
    // Only whole sentences the catalog has are replaced; anything else stays exactly as written.
    for (const auto* kept : { "Song 1. Song 2.", "Integrated loudness (I): the whole song. Unknown sentence here." })
        require (japanese (kept) == kept, "a text the catalog does not cover sentence by sentence stays as written");
    require (japanese ("PRE. Unknown sentence. Another one.") == juce::String::fromUTF8 (u8"PREのUnknown sentence. Another one."),
             "an unknown value keeps its English after the translated side");
    require (i18n::translate (notes[0] + " " + notes[1], i18n::Language::english) == notes[0] + " " + notes[1],
             "English stays exactly as written");
}
}
