#pragma once

#include <juce_core/juce_core.h>

// Kirin OS trims every name it publishes with JavaScript's \s plus U+0085, U+180E and U+200B, so
// a name it sends never starts or ends with one of these. Hypha checks that same set itself. JUCE's
// trim() asks the C library instead, which on Windows sees only the low 16 bits of a character and
// so takes a kanji such as U+2000B, or U+20020, for a space: a valid Preset or CHECK name would
// then reject the whole library there and nowhere else.
namespace hypha::reference_text
{
constexpr bool edgeSpace (juce::juce_wchar c) noexcept
{
    return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680 || c == 0x180e
        || (c >= 0x2000 && c <= 0x200b) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f
        || c == 0x3000 || c == 0xfeff;
}

// No such character at either end (an empty text has none).
inline bool trimmed (const juce::String& text) noexcept
{
    return text.isEmpty() || (! edgeSpace (text[0]) && ! edgeSpace (text.getLastCharacter()));
}

inline juce::String trimEdges (const juce::String& text)
{
    int start = 0, end = text.length();
    while (start < end && edgeSpace (text[start])) ++start;
    while (end > start && edgeSpace (text[end - 1])) --end;
    return text.substring (start, end);
}

// The first `maximum` characters, with no space left at the end: Kirin OS reads no name ending in one.
inline juce::String cut (const juce::String& text, int maximum)
{
    return trimEdges (text.substring (0, maximum));
}
}
