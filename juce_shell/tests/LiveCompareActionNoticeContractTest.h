#pragma once

#include "../src/HyphaLiveCompareActionResult.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyLiveCompareActionNotices()
{
    observatory::View post (observatory::Role::post);
    observatory::LiveCompareFooter rail;
    rail.active = rail.matched = rail.pinAvailable = true;
    rail.preGainTenthsDb = -125;
    rail.postHeldTenthsDb = -100;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (auto preset : observatory::sizePresets)
            for (auto readiness : { live_compare_ui::AutoReadiness::timing,
                                    live_compare_ui::AutoReadiness::match, live_compare_ui::AutoReadiness::limited })
            {
                post.setSize (preset.width, preset.height);
                post.setLiveCompareFooter (rail);
                const auto* notice = live_compare_ui::autoReadinessNotice (readiness);
                post.setFeedback (notice);
                const auto font = monoFont (post.presentationContext(), post.statusStripFolded()
                    ? typography::TextRole::status : typography::TextRole::action);
                const auto shown = text_style::shownText (notice);
                const auto width = text_style::shownWidth (font, notice);
                const auto image = post.createComponentSnapshot (post.getLocalBounds());
                if (shown.isEmpty() || width <= 0 || width > post.statusStripBounds().getWidth() - 12
                    || ! image.isValid() || (language == i18n::Language::japanese && shown == notice))
                {
                    std::cerr << "AUTO refusal screen lint: " << preset.width << " " << shown
                              << " width=" << width << " available=" << post.statusStripBounds().getWidth() - 12 << '\n';
                    std::abort();
                }
            }
    }
}
}
