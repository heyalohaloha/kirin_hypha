#pragma once
#include "ReferenceOverlayTest.h"

// 重ね比べの直し：曲が無くても目標があれば出す。
namespace hypha::tests::reference_overlay
{
// No song in the set but dropped targets: the list, ONE / ALL and Balance still show; ONE's chart says why it has
// nothing to draw, ALL compares the targets with A. Nothing at all: no list and no switch, as before.
inline void verifyTargetsWithoutSongs()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        reference_ui::Component panel; reference_text_repairs::sizePanel (panel, 900, 600);
        auto state = page (0, 2);
        panel.setState (state);
        auto& list = listOf (panel);
        require (list.rows().empty() && panel.findChildWithID ("reference-overlay")->isVisible(),
                 "targets without songs keep the list and ONE / ALL");
        auto drawn = a_direction::drawnTexts (panel);
        reference_text_repairs::requireWhole (drawn, "NO SONGS IN THIS SET");
        reference_text_repairs::requireWhole (drawn, "ALL SHOWS TARGETS");
        reference_text_repairs::requireWhole (drawn, reference_ui::aWindowLegend (state.aKirin->frames));
        switchHalf (panel, "reference-overlay-all").onClick();
        require (list.rows().size() == 2 && list.rows()[0].target && boxesOf (list).size() == 2, "ALL lists the targets");
        boxesOf (list)[0]->onClick();
        drawn = a_direction::drawnTexts (panel);
        reference_text_repairs::requireWhole (drawn, "1 Drop 1");
        require (! drawn.contains (text_style::shownText (reference_ui::overlayEmptyText)), "a shown target is drawn with A");
        drawingPart (panel, "reference-overlay-all-shape").onClick();
        reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), "MEAN OF ALL 2");
    }
    reference_ui::Component empty; reference_text_repairs::sizePanel (empty, 900, 600);
    empty.setState (page (0, 0));
    require (! empty.findChildWithID ("reference-song-list")->isVisible() && ! empty.findChildWithID ("reference-overlay")->isVisible(),
             "with nothing to draw there is no list and no switch (the B step says why)");
}

inline void verifyRepairs()
{
    verifyTargetsWithoutSongs();
    std::cout << "Reference Balance overlay repairs: PASS\n";
}
}
