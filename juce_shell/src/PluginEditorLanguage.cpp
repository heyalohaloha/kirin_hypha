#include "PluginEditor.h"

#include <functional>

namespace
{
    constexpr int languageEnglishAction = 30;
    constexpr int languageJapaneseAction = 31;
}

void KirinHyphaEditor::syncLanguage (bool layOutOnChange)
{
    // A hosted editor shows the language chosen in MENU, or the system's until a choice is made.
    // Tests and tools keep the language they set (English unless they set one): they run outside
    // a plug-in wrapper, or hold the language while they simulate one.
    if (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Undefined
        && ! hypha::i18n::languageHeld())
        hypha::i18n::setCurrent (hypha::LanguagePreference::shared().language());
    if (appliedLanguageRevision == hypha::i18n::revision())
        return;
    appliedLanguageRevision = hypha::i18n::revision();
    if (layOutOnChange)
        applyLanguage();
}

void KirinHyphaEditor::applyLanguage()
{
    // Components keep their English and translate where they draw (INV-S40), but text widths
    // change with the language: every component lays out again inside its current bounds, then
    // the editor repaints. Menus, tooltips and the feedback strip translate as they are shown.
    std::function<void (juce::Component&)> layOut = [&layOut] (juce::Component& component)
    {
        component.resized();
        for (auto* child : component.getChildren())
            layOut (*child);
    };
    layOut (*this);
    repaint();
}

void KirinHyphaEditor::addLanguageMenu (juce::PopupMenu& menu) const
{
    // Each language is named in itself, so either can be found whichever one is showing.
    const auto japanese = hypha::i18n::current() == hypha::i18n::Language::japanese;
    juce::PopupMenu languages;
    languages.addItem (languageEnglishAction, "English", true, ! japanese);
    languages.addItem (languageJapaneseAction, juce::String::fromUTF8 (u8"日本語"), true, japanese);
    menu.addSubMenu ("Language", languages);
}

bool KirinHyphaEditor::handleLanguageMenu (int result)
{
    if (result != languageEnglishAction && result != languageJapaneseAction)
        return false;
    const auto language = result == languageJapaneseAction ? hypha::i18n::Language::japanese
                                                           : hypha::i18n::Language::english;
    const auto persisted = hypha::i18n::languageHeld()
        || hypha::LanguagePreference::shared().setLanguage (language);
    hypha::i18n::setCurrent (language);
    syncLanguage();
    // An explicit choice that could not be saved still holds until Hypha closes; say so (R-28).
    if (! persisted)
        showToast ("Language changed for this session only");
    return true;
}
