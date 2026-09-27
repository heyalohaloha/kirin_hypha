#pragma once

#include "../src/HyphaLanguage.h"

#include <iostream>
#include <set>

// Collects the text that reached a Japanese screen with no catalog entry (HyphaLanguage.h), so a
// review run lists prose the catalog is missing. Labels, units and values are expected there.
namespace hypha::tests::language_misses
{
inline std::set<juce::String>& collected()
{
    static std::set<juce::String> misses;
    return misses;
}

struct Scope
{
    Scope() { i18n::observeMisses ([] (const juce::String& text) { collected().insert (text); }); }
    ~Scope() { i18n::observeMisses (nullptr); }
};

// Prints what looks like prose: lowercase words, or several upper-case words.
inline void report (const char* where)
{
    for (const auto& text : collected())
    {
        auto words = 0;
        auto lower = false;
        for (const auto& token : juce::StringArray::fromTokens (text, " /", {}))
        {
            if (token.containsAnyOf ("abcdefghijklmnopqrstuvwxyz")) lower = true;
            if (token.length() >= 2 && token.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZ'-")) ++words;
        }
        if (lower || words >= 2)
            std::cout << "Untranslated (" << where << "): " << text << '\n';
    }
    collected().clear();
}
}
