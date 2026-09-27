#pragma once

#include <juce_core/juce_core.h>

// Hypha's screen text is written in English in the source. In Japanese, the text that explains or
// reports (status lines, help, guidance, menus, dialog text) is shown from a catalog keyed by that
// English (HyphaJapaneseCatalog.h). Labels, abbreviations, units, values and names that come from
// outside stay as they are (INV-S40).
//
// Translation happens where text meets the screen: the text_style drawing calls, the tooltip
// window, menus, labels and buttons. The source keeps its English, so a language change needs
// only a new layout and a repaint, and English output is byte for byte what it was.
namespace hypha::i18n
{
enum class Language : int
{
    english = 0,
    japanese = 1,
};

// The language every open Hypha editor in this binary shows. Editors hosted as a plug-in follow
// the saved choice, or the system language when there is none (HyphaUiPreferences.h). Anything
// else (tests, tools) stays in English unless it sets a language itself.
Language current() noexcept;
void setCurrent (Language) noexcept;
// Changes whenever the language changes, so an editor can tell that it has to lay out again.
unsigned int revision() noexcept;

// Tests and tools that open a shipping editor under a simulated plug-in wrapper hold the language
// they set, so the user's saved choice and the system language never reach their expectations.
void holdLanguage (bool held) noexcept;
bool languageHeld() noexcept;

// The system's display language ("ja-JP", "en-US") mapped to a Hypha language.
Language languageForSystem (const juce::String& displayLanguage) noexcept;

// `english` in the current language: its catalog text, the catalog pattern it matches with the
// values put back, each " / " part translated on its own, or `english` itself when the catalog
// has none of these (a label, a unit, a value, a name).
juce::String tr (const juce::String& english);
juce::String translate (const juce::String& english, Language);

// True when the catalog holds `english` itself or a pattern that matches it whole.
bool hasTranslation (const juce::String& english);

// Tests can watch the text that reached a Japanese screen with no catalog entry for it or for any
// of its parts, to find prose the catalog is missing. Not used by the product.
using MissObserver = void (*) (const juce::String&);
void observeMisses (MissObserver) noexcept;

// Sets a language for the lifetime of the object and puts the previous one back (tests, renders).
class ScopedLanguage final
{
public:
    explicit ScopedLanguage (Language language) noexcept : previous (current())
    {
        setCurrent (language);
    }
    ~ScopedLanguage() { setCurrent (previous); }

private:
    Language previous;

    JUCE_DECLARE_NON_COPYABLE (ScopedLanguage)
};
}
