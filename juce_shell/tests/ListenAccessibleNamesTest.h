#pragma once

#include "../src/HyphaLanguage.h"
#include "../src/HyphaHelpLineText.h"
#include "../src/HyphaObservatoryView.h"
#include <juce_gui_basics/detail/juce_ButtonAccessibilityHandler.h>
#include <stdexcept>

namespace hypha::tests
{
inline void verifyListenAccessibleNames()
{
    observatory::View view (observatory::Role::post);
    view.setSize (900, 600);
    observatory::LiveCompareFooter state;
    state.active = true;
    view.setLiveCompareFooter (state);
    auto* pre = dynamic_cast<juce::Button*> (view.findChildWithID ("observatory-live-pre"));
    auto* post = dynamic_cast<juce::Button*> (view.findChildWithID ("observatory-live-post"));
    auto* target = dynamic_cast<juce::Button*> (view.findChildWithID ("observatory-target"));
    if (pre == nullptr || post == nullptr || target == nullptr) throw std::runtime_error ("LISTEN controls missing");
    juce::detail::ButtonAccessibilityHandler preAx (*pre, juce::AccessibilityRole::button);
    juce::detail::ButtonAccessibilityHandler postAx (*post, juce::AccessibilityRole::button);
    juce::detail::ButtonAccessibilityHandler targetAx (*target, juce::AccessibilityRole::button);
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        view.resized();
        const auto preTitle = language == i18n::Language::japanese ? juce::String::fromUTF8 (u8"LISTEN：PREを再生") : "LISTEN: play PRE";
        const auto postTitle = language == i18n::Language::japanese ? juce::String::fromUTF8 (u8"LISTEN：POSTを再生") : "LISTEN: play POST";
        if (preAx.getTitle() != preTitle || postAx.getTitle() != postTitle)
            throw std::runtime_error ("LISTEN source titles must be distinct and localized");
        if (pre->getButtonText() != "PRE" || post->getButtonText() != "POST")
            throw std::runtime_error ("LISTEN visible labels changed");
        if (targetAx.getTitle() != "POST")
            throw std::runtime_error ("measurement POST title changed");
        const auto postHelp = language == i18n::Language::japanese
            ? juce::String::fromUTF8 (u8"このチェーンの出力、POSTを聴きます") : "Listen to POST, the output of this chain";
        if (post->getDescription() != postHelp || postAx.getHelp() != postHelp)
            throw std::runtime_error ("LISTEN POST description and accessible help must be localized");
        if (pre->getDescription() != i18n::tr ("Listen to PRE, the input of this chain. MATCH levels it to POST")
            || preAx.getHelp() != pre->getDescription())
            throw std::runtime_error ("LISTEN PRE description and accessible help must be localized");
        if (help_line::forLine (pre->getTooltip(), false) != "Listen to PRE, the input of this chain")
            throw std::runtime_error ("localized help still has a short footer line");
    }
    state.candidate = true;
    view.setLiveCompareFooter (state);
    if (preAx.getTitle() != "A" || postAx.getTitle() != "B")
        throw std::runtime_error ("A/B keeps its own source titles");
}
}
