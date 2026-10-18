#pragma once

#include "../src/HyphaAnalysisUiText.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaSpectrumUiContract.h"
#include "../src/HyphaTheme.h"
#include "../src/HyphaUiContract.h"

#include <cstdlib>
#include <iostream>

// 2026-10-18: a view that waits for one of the two analysis slots says who holds them and
// how one frees, on the view itself in two lines at most, at every size and in both languages. A
// holder whose name could not be read is "another Hypha", never left out or invented.
namespace hypha::tests
{
inline void verifyAnalysisSlotOwners()
{
    const auto require = [] (bool valid, const char* message)
    {
        if (valid) return;
        std::cerr << "Analysis slot owners: " << message << '\n';
        std::exit (EXIT_FAILURE);
    };
    require (analysis_ui::slotOwnerNames ({ "Mix", "Vocal" }) == "Mix, Vocal", "both holders by name");
    require (analysis_ui::slotOwnerNames ({ "POST 3fa9c1d2", "" }) == "POST 3fa9c1d2, another Hypha",
             "a holder that could not be named is still counted");
    require (analysis_ui::slotOwnerNames ({ "", "" }) == "another Hypha, another Hypha", "two unnamed holders");
    {
        const i18n::ScopedLanguage japanese (i18n::Language::japanese);
        const auto owners = analysis_ui::slotOwnerNames ({ "POST 3fa9c1d2", "" });
        require (owners == juce::String::fromUTF8 (u8"POST 3fa9c1d2, 別のHypha")
                     && ! text_style::shownText (analysis_ui::slotsInUseStatus (owners)).contains ("another"),
                 "in Japanese the unnamed holder reads in Japanese and the name stays");
    }
    const auto named = analysis_ui::slotsInUseStatus ("Mix, POST 3fa9c1d2");
    require (named == "ANALYSIS IN USE / Mix, POST 3fa9c1d2 / SET ONE TO LEVEL OR CLOSE IT",
             "the status names the holders and how one frees");
    require (analysis_ui::slotsInUseStatus ({}) == "ANALYSIS IN USE / SET ANOTHER VIEW TO LEVEL OR CLOSE IT",
             "without names it still says how a slot frees");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto& status : { named, analysis_ui::slotsInUseStatus ({}),
                                    analysis_ui::slotsInUseStatus ("another Hypha, another Hypha") })
        {
            const auto shown = text_style::shownText (status);
            require (language == i18n::Language::english || (! shown.contains ("SET ") && shown.contains ("POST 3fa9c1d2") == status.contains ("POST 3fa9c1d2")),
                     "the Japanese status translates the words and keeps the names");
            for (const auto& size : ui_contract::spectrumSizePresets)
            {
                const auto context = presentation::forEditor (size.width, size.height);
                const auto plot = ui_contract::spectrumPlotBounds (size.width, size.height);
                const auto width = plot.width - ui_contract::spectrumPlotLeftInset - ui_contract::spectrumPlotRightInset;
                const auto font = monoFont (context, typography::TextRole::status, typography::Composition::visualization);
                const auto lines = text_style::shownWrappedHeight (status, font, width) / font.getHeight();
                require (lines <= 2.5f, "the waiting status fits the view in two lines at every size");
            }
        }
    }
}
}
