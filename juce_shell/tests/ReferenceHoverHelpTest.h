#pragma once

// 2026-10-04：300% の B・C・V で項目を指すと、下の状態の
// 行がその説明の一行になる（HyphaReferenceHelp.h）。説明はどれも 300% の足元の行に両言語で省略せずに収まる。
#include "ReferenceGuideContractTest.h"
#include "../src/HyphaReferenceHelpText.h"

#include <algorithm>
#include <iterator>
#include <set>

namespace hypha::tests
{
// 描いて（図が説明の場所を添える）、REF の中を 6 px ごとに指したときに出る説明（英語）。
inline std::set<juce::String> helpTextsOf (reference_ui::Component& panel)
{
    panel.createComponentSnapshot (panel.getLocalBounds());
    std::set<juce::String> texts;
    for (int y = 0; y < panel.getHeight(); y += 6)
        for (int x = 0; x < panel.getWidth(); x += 6)
            if (const auto text = panel.helpAt ({ x, y }); text.isNotEmpty()) texts.insert (text);
    return texts;
}

// 足元の行（300%、エディターが置く場所）に、説明が両言語で収まる。`extra` は部品の説明（ツールチップ）。
inline void verifyReferenceHelpFits (const std::set<juce::String>& extra)
{
    using namespace reference_guide_contract;
    observatory::View shell (observatory::Role::post);
    shell.setSize (900, 600);
    shell.setDomain (observatory::Domain::reference);
    shell.setExternalAnalysisBodyActive (true);
    const auto line = shell.statusStripBounds().reduced (4, 0);
    const auto wholeRow = shell.footerBounds().reduced (8, 0);
    require (line.getWidth() > 300, "300% has a footer line for the help");
    require (wholeRow.getWidth() > line.getWidth(), "graph help has the whole footer row");
    const auto context = presentation::forEditor (900, 600);
    std::set<juce::String> texts (extra);
    for (const auto* text : reference_ui::help_text::all) texts.insert (text);
    juce::String tooWide;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        const auto font = labelFont (context, typography::TextRole::unit, typography::Composition::information);
        for (const auto& text : texts)
        {
            // Graphs, tabs and lists are in the page body; the editor gives their help the full
            // footer row. Keep the narrower control-help check for the other collected tooltips.
            const bool inBody = std::any_of (std::begin (reference_ui::help_text::all), std::end (reference_ui::help_text::all),
                                             [&] (const auto* help) { return text == help; });
            const auto available = inBody ? wholeRow.getWidth() : line.getWidth();
            if (const auto width = text_style::shownWidth (font, text); width > static_cast<float> (available))
                tooWide << "\n  " << (language == i18n::Language::japanese ? "JA " : "EN ") << juce::String (width, 0) << " > "
                        << available << ": " << text_style::shownText (text);
        }
    }
    require (tooWide.isEmpty(), "every help fits the footer line at 300%:" + tooWide);
}
}
