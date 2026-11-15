#pragma once
#include <juce_core/juce_core.h>

// Fixed ASCII grammars used by reference workers. These have no destructible static state.
namespace hypha::reference_audition::validation
{
inline bool digit (juce::juce_wchar c) noexcept { return c >= '0' && c <= '9'; }
inline bool hex (juce::juce_wchar c, bool upper) noexcept
{ return digit (c) || (c >= 'a' && c <= 'f') || (upper && c >= 'A' && c <= 'F'); }
inline bool id (const juce::String& text) noexcept
{
    if (text.isEmpty() || text.length() > 160) return false;
    for (const auto c : text)
        if (!digit (c) && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z')
            && c != '.' && c != '_' && c != ':' && c != '-') return false;
    return true;
}
inline bool sha256 (const juce::String& text) noexcept
{
    if (text.length() != 64) return false;
    for (const auto c : text) if (!hex (c, false)) return false;
    return true;
}
enum class Uuid { anyLower, anyCase, v4Lower, versioned };
inline bool uuid (const juce::String& text, Uuid kind) noexcept
{
    if (text.length() != 36) return false;
    const bool upper = kind == Uuid::anyCase || kind == Uuid::versioned;
    for (int i = 0; i < 36; ++i)
    {
        const auto c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!hex (c, upper)) return false;
    }
    if (kind == Uuid::versioned && (text[14] < '1' || text[14] > '5')) return false;
    if (kind == Uuid::v4Lower && text[14] != '4') return false;
    if (kind == Uuid::versioned || kind == Uuid::v4Lower)
    {
        const auto c = text[19];
        if (c != '8' && c != '9' && c != 'a' && c != 'b'
            && !(upper && (c == 'A' || c == 'B'))) return false;
    }
    return true;
}
inline bool decimal (const juce::String& text) noexcept
{
    if (text.isEmpty()) return false;
    bool dot = false, lastDigit = false;
    for (const auto c : text)
    {
        if (digit (c)) { lastDigit = true; continue; }
        if (c != '.' || dot || !lastDigit) return false;
        dot = true; lastDigit = false;
    }
    return lastDigit;
}
inline bool timestamp (const juce::String& text) noexcept
{
    if (text.length() != 24 || text[4] != '-' || text[7] != '-' || text[10] != 'T'
        || text[13] != ':' || text[16] != ':' || text[19] != '.' || text[23] != 'Z') return false;
    for (const int i : { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18, 20, 21, 22 })
        if (!digit (text[i])) return false;
    const auto value = [&text] (int i) { return (text[i] - '0') * 10 + text[i + 1] - '0'; };
    return value (5) >= 1 && value (5) <= 12 && value (8) <= 31
        && value (11) <= 23 && value (14) <= 59 && value (17) <= 59;
}
}
