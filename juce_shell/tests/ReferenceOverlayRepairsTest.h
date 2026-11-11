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

// Only the very first ALL turns every song on. A selection the user emptied stays empty, also after ONE and
// after the project is saved and opened again.
inline void verifyFirstUseOnly()
{
    reference_ui::Component panel; reference_text_repairs::sizePanel (panel, 900, 600);
    auto state = page (4, 0);
    panel.setState (state);
    Saved saved; saved.track (panel);
    switchHalf (panel, "reference-overlay-all").onClick();
    require (panel.state().overlay.lines.size() == 4 && panel.state().overlay.used && saved.overlay.used, "the first ALL turns the songs on");
    auto& list = listOf (panel);
    for (int round = 0; round < 4; ++round) boxesOf (list)[static_cast<size_t> (round)]->onClick();
    require (panel.state().overlay.lines.empty(), "every line turned off");
    switchHalf (panel, "reference-overlay-one").onClick();
    switchHalf (panel, "reference-overlay-all").onClick();
    require (panel.state().overlay.lines.empty(), "ALL again keeps the emptied selection");
    reference_text_repairs::requireWhole (a_direction::drawnTexts (panel), reference_ui::overlayEmptyText);
    juce::XmlElement project ("ReferenceChoices"); saved.overlay.write (project);
    auto reopened = state; reopened.overlay = ref::BalanceOverlay::read (project); reopened.overlay.all = false;
    reference_ui::Component again; reference_text_repairs::sizePanel (again, 900, 600);
    again.setState (reopened);
    switchHalf (again, "reference-overlay-all").onClick();
    require (again.state().overlay.lines.empty(), "a saved, emptied selection stays empty after reopening");
}

// Every number box state (shown or not, ALL SHAPE's dimmed, pointed, held) reads at 4.5:1 over its own fill on
// the page's ground and on the lightest list glass, and a line's number keeps its line's hue.
inline void verifyNumberContrast()
{
    float lowest = 100.0f;
    for (int colour = 0; colour < 32; ++colour)
        for (const bool shown : { false, true })
            for (const bool dimmed : { false, true })
                for (const bool highlighted : { false, true })
                    for (const bool down : { false, true })
                    {
                        const auto look = reference_ui::numberLook (colour, shown, dimmed, highlighted, down);
                        for (const auto ground : reference_ui::numberGrounds())
                            lowest = juce::jmin (lowest, reference_ui::contrastRatio (look.text, ground.overlaidWith (look.fill)));
                    }
    require (lowest >= 4.5f, "every number reads at 4.5:1 over its fill: " + juce::String (lowest, 2));
    const auto held = reference_ui::numberLook (7, true, false, true, true);
    require (std::abs (held.text.getHue() - reference_ui::overlayTextColour (7).getHue()) < 0.04f && held.text.getSaturation() > 0.15f,
             "a held number keeps its line's hue");
}

// Pointing at the chart in SHAPE reads the mean in the screen's language (平均 -30.0), and the legend's counts
// keep their own wording (the reading's pattern never takes "MEAN OF 3").
inline void verifyMeanReading()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        const bool japanese = language == i18n::Language::japanese;
        require (text_style::shownText ("MEAN -30.0") == (japanese ? juce::String::fromUTF8 (u8"平均 -30.0") : juce::String ("MEAN -30.0"))
                 && text_style::shownText ("MEAN OF 3") == (japanese ? juce::String::fromUTF8 (u8"3曲の平均") : juce::String ("MEAN OF 3"))
                 && text_style::shownText ("MEAN OF ALL 6") == (japanese ? juce::String::fromUTF8 (u8"全6曲の平均") : juce::String ("MEAN OF ALL 6")),
                 "the mean reading and the legend's counts read in the screen's language");
        reference_ui::Component panel; reference_text_repairs::sizePanel (panel, 900, 600);
        panel.setState (page (4, 0));
        switchHalf (panel, "reference-overlay-all").onClick();
        drawingPart (panel, "reference-overlay-shape").onClick();
        auto* overlay = panel.findChildWithID ("reference-overlay");
        const juce::Point<float> at { static_cast<float> (overlay->getX() - 80), static_cast<float> (overlay->getBottom() + 120) };
        panel.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, &panel, &panel, juce::Time(), at, juce::Time(), 0, false));
        bool read = false;
        for (const auto& text : a_direction::drawnTexts (panel))
            read = read || text.startsWith (japanese ? juce::String::fromUTF8 (u8"平均 ") : juce::String ("MEAN "));
        require (read, "pointing at SHAPE's chart reads the mean in the screen's language");
    }
}

inline void verifyRepairs()
{
    verifyMeanReading();
    verifyNumberContrast();
    verifyTargetsWithoutSongs();
    verifyFirstUseOnly();
    std::cout << "Reference Balance overlay repairs: PASS\n";
}
}
