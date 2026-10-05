#include "HyphaLanguage.h"

#include "HyphaJapaneseCatalog.h"

#include <array>
#include <atomic>
#include <unordered_map>

namespace hypha::i18n
{
namespace
{
std::atomic<int> currentLanguage { static_cast<int> (Language::english) };
std::atomic<unsigned int> languageRevision { 0u };
std::atomic<bool> languageIsHeld { false };
std::atomic<MissObserver> missObserver { nullptr };

// An entry whose English carries %1 to %3. `literals` holds the fixed text around the values, one
// more than `slots`, which numbers the values in the order they appear in the English.
struct Pattern
{
    std::vector<juce::String> literals;
    std::vector<int> slots;
    juce::String japanese;
};

struct Catalog
{
    std::unordered_map<juce::String, juce::String> exact;
    std::vector<Pattern> patterns;
};

bool compilePattern (const juce::String& english, const juce::String& japanese, Pattern& pattern)
{
    juce::String literal;
    for (int index = 0; index < english.length(); ++index)
    {
        const auto character = english[index];
        const auto next = index + 1 < english.length() ? english[index + 1] : 0;
        if (character == '%' && next >= '1' && next <= '3')
        {
            pattern.literals.push_back (literal);
            pattern.slots.push_back (static_cast<int> (next - '0'));
            literal.clear();
            ++index;
            continue;
        }
        literal += juce::String::charToString (character);
    }
    pattern.literals.push_back (literal);
    pattern.japanese = japanese;
    return ! pattern.slots.empty();
}

const Catalog& japaneseCatalog()
{
    static const Catalog lookup = []
    {
        Catalog built;
        for (const auto& section : catalog::sections())
            for (std::size_t index = 0; index < section.count; ++index)
            {
                const auto english = juce::String::fromUTF8 (section.entries[index].english);
                const auto japanese = juce::String::fromUTF8 (section.entries[index].japanese);
                Pattern pattern;
                if (compilePattern (english, japanese, pattern))
                    built.patterns.push_back (std::move (pattern));
                else
                    built.exact.emplace (english, japanese);
            }
        return built;
    }();
    return lookup;
}

bool isFactSeparator (juce::juce_wchar character) noexcept
{
    return character == '/' || character == 0x00b7;
}

// " / " and "  ·  " join separate facts (translateParts).
bool joinsFacts (const juce::String& text) noexcept
{
    for (int index = 1; index + 1 < text.length(); ++index)
        if (isFactSeparator (text[index]) && text[index - 1] == ' ' && text[index + 1] == ' ')
            return true;
    return false;
}

// Values are found left to right: each one runs up to the next fixed text, the last one up to the
// fixed text that ends the English. A value is never empty.
bool matchPattern (const Pattern& pattern, const juce::String& text,
                   std::array<juce::String, 4>& values)
{
    const auto& first = pattern.literals.front();
    const auto& last = pattern.literals.back();
    if (! text.startsWith (first) || ! text.endsWith (last)
        || text.length() < first.length() + last.length() + 1)
        return false;
    const auto valuesEnd = text.length() - last.length();
    auto position = first.length();
    for (std::size_t index = 0; index < pattern.slots.size(); ++index)
    {
        const auto finalValue = index + 1 == pattern.slots.size();
        const auto& following = pattern.literals[index + 1];
        const auto valueEnd = finalValue ? valuesEnd : text.indexOf (position + 1, following);
        if (valueEnd <= position || valueEnd > valuesEnd)
            return false;
        // A value is one piece of one line; several lines are translated one by one.
        const auto value = text.substring (position, valueEnd);
        if (value.containsChar ('\n'))
            return false;
        values[static_cast<std::size_t> (pattern.slots[index])] = value;
        position = finalValue ? valueEnd : valueEnd + following.length();
    }
    return position == valuesEnd;
}

// A value inside a pattern can itself be catalog prose, a state inside a sentence ("B: %1" around
// "KIRIN OS PREPARES 2 SONGS FIRST"): it is translated whole, by a pattern of its own, or part by
// part, down to this many sentences deep. Below that only a whole entry is looked up.
constexpr int nestingLimit = 3;

juce::String translateParts (const Catalog&, const juce::String&, int depth);

juce::String exactOrPattern (const Catalog&, const juce::String&, bool& found, int depth = 0);

juce::String nestedValue (const Catalog& catalog, const juce::String& value, int depth)
{
    if (depth >= nestingLimit)
    {
        const auto nested = catalog.exact.find (value);
        return nested != catalog.exact.end() ? nested->second : value;
    }
    bool found = false;
    const auto whole = exactOrPattern (catalog, value, found, depth + 1);
    return found ? whole : translateParts (catalog, value, depth + 1);
}

juce::String exactOrPattern (const Catalog& catalog, const juce::String& text, bool& found, int depth)
{
    found = true;
    if (const auto entry = catalog.exact.find (text); entry != catalog.exact.end())
        return entry->second;
    for (const auto& pattern : catalog.patterns)
    {
        std::array<juce::String, 4> values;
        if (! matchPattern (pattern, text, values))
            continue;
        auto result = pattern.japanese;
        bool spansFacts = false;
        for (const auto slot : pattern.slots)
        {
            const auto& value = values[static_cast<std::size_t> (slot)];
            // A value holds one fact, or several facts the catalog has as one sentence ("V: %1" around a
            // "reason / fix"). "OUTSIDE %1 CUE" must not take "B CUE / MOVE OR CHOOSE LONGER" as its value:
            // such a text is translated fact by fact instead.
            if (joinsFacts (value))
            {
                const auto sentence = catalog.exact.find (value);
                spansFacts = sentence == catalog.exact.end();
                if (spansFacts) break;
                result = result.replace ("%" + juce::String (slot), sentence->second);
                continue;
            }
            result = result.replace ("%" + juce::String (slot), nestedValue (catalog, value, depth));
        }
        if (spansFacts)
            continue;
        return result;
    }
    found = false;
    return text;
}

// "PART / PART" statuses and "PART  ·  PART" Guide facts join separate facts; each part is looked
// up on its own when the whole is not in the catalog. The spaces around each separator are kept.
juce::String translateParts (const Catalog& catalog, const juce::String& text, int depth)
{
    juce::String result;
    auto translatedAny = false;
    auto start = 0;
    for (auto slash = 0; slash < text.length(); ++slash)
    {
        if (! isFactSeparator (text[slash]) || slash == 0 || slash + 1 >= text.length()
            || text[slash - 1] != ' ' || text[slash + 1] != ' ')
            continue;
        if (slash - 1 < start)
            continue;
        auto partEnd = slash - 1;
        while (partEnd > start && text[partEnd - 1] == ' ')
            --partEnd;
        auto nextStart = slash + 2;
        while (nextStart < text.length() && text[nextStart] == ' ')
            ++nextStart;
        bool found = false;
        result += exactOrPattern (catalog, text.substring (start, partEnd), found, depth);
        translatedAny = translatedAny || found;
        result += text.substring (partEnd, nextStart);
        start = nextStart;
    }
    if (start == 0)
        return text;
    bool found = false;
    result += exactOrPattern (catalog, text.substring (start), found, depth);
    return translatedAny || found ? result : text;
}

// A name Kirin OS ranked for Hypha ("Mastering · tone, level, and dynamics   1 / 3", HyphaReferenceRuntimeView.h and
// PluginEditorReferenceRoles.cpp) reads the name in the catalog and keeps the rank. Empty when the text is not one.
juce::String translateRanked (const Catalog& catalog, const juce::String& text)
{
    const auto split = text.lastIndexOf ("   ");
    if (split <= 0) return {};
    const auto rank = text.substring (split + 3);
    const auto slash = rank.indexOf (" / ");
    const auto place = rank.substring (0, juce::jmax (0, slash)), count = rank.substring (slash + 3);
    if (slash <= 0 || count.isEmpty() || ! place.containsOnly ("0123456789") || ! count.containsOnly ("0123456789"))
        return {};
    bool found = false;
    const auto name = exactOrPattern (catalog, text.substring (0, split), found);
    return found ? name + text.substring (split) : juce::String {};
}

juce::String translateLine (const Catalog& catalog, const juce::String& line)
{
    bool found = false;
    const auto whole = exactOrPattern (catalog, line, found);
    return found ? whole : translateParts (catalog, line, 0);
}
}

Language current() noexcept
{
    return static_cast<Language> (currentLanguage.load (std::memory_order_relaxed));
}

void setCurrent (Language language) noexcept
{
    const auto previous = currentLanguage.exchange (static_cast<int> (language),
                                                    std::memory_order_relaxed);
    if (previous != static_cast<int> (language))
        languageRevision.fetch_add (1u, std::memory_order_relaxed);
}

unsigned int revision() noexcept
{
    return languageRevision.load (std::memory_order_relaxed);
}

void holdLanguage (bool held) noexcept
{
    languageIsHeld.store (held, std::memory_order_relaxed);
}

bool languageHeld() noexcept
{
    return languageIsHeld.load (std::memory_order_relaxed);
}

Language languageForSystem (const juce::String& displayLanguage) noexcept
{
    const auto code = displayLanguage.trim().toLowerCase();
    return code == "ja" || code.startsWith ("ja-") || code.startsWith ("ja_")
        ? Language::japanese : Language::english;
}

juce::String translate (const juce::String& english, Language language)
{
    if (language == Language::english || english.isEmpty())
        return english;
    const auto& catalog = japaneseCatalog();
    bool found = false;
    const auto whole = exactOrPattern (catalog, english, found);
    if (found)
        return whole;
    if (const auto ranked = translateRanked (catalog, english); ranked.isNotEmpty())
        return ranked;
    // A detail built from several lines (a failure, then what was kept) translates line by line.
    if (! english.containsChar ('\n'))
    {
        const auto parts = translateParts (catalog, english, 0);
        if (auto* observer = missObserver.load (std::memory_order_relaxed); observer != nullptr
            && parts == english)
            observer (english);
        return parts;
    }
    juce::StringArray lines;
    lines.addTokens (english, "\n", {});
    for (auto& line : lines)
        line = translateLine (catalog, line);
    return lines.joinIntoString ("\n");
}

juce::String tr (const juce::String& english)
{
    return translate (english, current());
}

void observeMisses (MissObserver observer) noexcept
{
    missObserver.store (observer, std::memory_order_relaxed);
}

bool hasTranslation (const juce::String& english)
{
    bool found = false;
    exactOrPattern (japaneseCatalog(), english, found);
    return found;
}
}
