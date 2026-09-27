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

juce::String exactOrPattern (const Catalog& catalog, const juce::String& text, bool& found)
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
        for (int slot = 1; slot <= 3; ++slot)
        {
            // A value that is itself catalog prose (a state inside a sentence) is shown translated.
            const auto& value = values[static_cast<std::size_t> (slot)];
            const auto nested = catalog.exact.find (value);
            result = result.replace ("%" + juce::String (slot),
                                     nested != catalog.exact.end() ? nested->second : value);
        }
        return result;
    }
    found = false;
    return text;
}

bool isFactSeparator (juce::juce_wchar character) noexcept
{
    return character == '/' || character == 0x00b7;
}

// "PART / PART" statuses and "PART  ·  PART" Guide facts join separate facts; each part is looked
// up on its own when the whole is not in the catalog. The spaces around each separator are kept.
juce::String translateParts (const Catalog& catalog, const juce::String& text)
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
        result += exactOrPattern (catalog, text.substring (start, partEnd), found);
        translatedAny = translatedAny || found;
        result += text.substring (partEnd, nextStart);
        start = nextStart;
    }
    if (start == 0)
        return text;
    bool found = false;
    result += exactOrPattern (catalog, text.substring (start), found);
    return translatedAny || found ? result : text;
}

juce::String translateLine (const Catalog& catalog, const juce::String& line)
{
    bool found = false;
    const auto whole = exactOrPattern (catalog, line, found);
    return found ? whole : translateParts (catalog, line);
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
    // A detail built from several lines (a failure, then what was kept) translates line by line.
    if (! english.containsChar ('\n'))
    {
        const auto parts = translateParts (catalog, english);
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
