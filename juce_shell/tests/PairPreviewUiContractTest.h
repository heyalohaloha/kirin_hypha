#pragma once
#include "../src/HyphaWidgets.h"
#include "../src/HyphaPairLabels.h"
#include "../src/HyphaTextStyle.h"
#include <cstdlib>
#include <cmath>
#include <iostream>

namespace hypha::tests
{
inline void verifyPairPreviewUiContract()
{
    const auto require = [] (bool ok, const char* message)
    { if (! ok) { std::cerr << "Pair preview UI: " << message << '\n'; std::exit (EXIT_FAILURE); } };
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (int percent : { 100, 150, 200, 300 })
        {
            EditableName badge;
            badge.setPresentationContext (presentation::forEditor (3 * percent, 2 * percent));
            badge.setSize (180 * percent / 100, 24 * percent / 100);
            badge.setFallback (pair_labels::unpaired);
            badge.onSelect = [] {};
            juce::Image image (juce::Image::ARGB, badge.getWidth(), badge.getHeight(), true);
            juce::Graphics graphics (image);
            for (int state : { 0, 1 })
            {
                badge.setModelName ({});
                require (state == 0 || badge.setSelectionPreview (pair_labels::candidateLabel ("Mix", "01234567"), 1), "candidate fits inside border");
                text_style::ShownTextLog shown; badge.paint (graphics);
                require (! shown.areas().empty(), "badge text has an area");
                const auto area = shown.areas().back();
                require (area.getX() >= 2 && area.getRight() <= badge.getWidth() - 2,
                         "unpaired text stays inside both border edges");
                bool copperText = false;
                for (int y = 3; y < badge.getHeight() - 3 && ! copperText; ++y)
                    for (int x = 5; x < badge.getWidth() - 5; ++x)
                    {
                        const auto pixel = image.getPixelAt (x, y);
                        if (pixel.getAlpha() > 130 && std::abs (int (pixel.getRed()) - int (COL_COPPER.getRed())) < 8
                            && std::abs (int (pixel.getGreen()) - int (COL_COPPER.getGreen())) < 8
                            && std::abs (int (pixel.getBlue()) - int (COL_COPPER.getBlue())) < 8)
                        { copperText = true; break; }
                    }
                require (copperText, "unpaired text uses copper");
                const auto edge = image.getPixelAt (0, badge.getHeight() / 2);
                require (std::abs (int (edge.getRed()) - int (COL_COPPER.getRed())) < 8
                         && std::abs (int (edge.getGreen()) - int (COL_COPPER.getGreen())) < 8
                         && std::abs (int (edge.getBlue()) - int (COL_COPPER.getBlue())) < 8,
                         "unpaired border uses copper");
            }
        }
    }
    {
        require (pair_labels::candidateLabel ("Mix", "0123456789") == "+ Mix", "candidate uses the product label");
        require (pair_labels::candidateLabel ({}, "0123456789") == "+ 01234567", "unnamed candidate uses short identity");
        const auto tip = pair_labels::candidateTooltip ("Mix", "01234567");
        require (tip == juce::String::fromUTF8 (u8"Mix / 01234567\nClick to connect\n▼ All PREs"), "candidate tooltip actions");
        require (i18n::translate (tip, i18n::Language::japanese).contains (juce::String::fromUTF8 (u8"押して接続"))
                 && i18n::translate (tip, i18n::Language::japanese).contains (juce::String::fromUTF8 (u8"▼ 候補一覧")),
                 "candidate tooltip actions are translated");
    }
    {
        require (i18n::translate (juce::String (pair_labels::menuPrefix) + "Mix", i18n::Language::japanese) == juce::String::fromUTF8 (u8"PRE：Mix"),
                 "candidate menu text is translated from its product form");
        EditableName paired; paired.setSize (90, 24); paired.onSelect = [] {};
        paired.setEnabledTooltip (pair_labels::choose); paired.setModelName ("Long Mix Name");
        require (paired.getTooltip().contains ("PRE  Long Mix Name"), "paired tooltip retains the full name");
        paired.setEnabledTooltip (pair_labels::choose);
        require (paired.getTooltip().contains ("PRE  Long Mix Name"), "tooltip update retains the full name");
        paired.setEditingEnabled (false);
        require (paired.getTooltip().contains ("PRE  Long Mix Name"), "locked paired tooltip retains the full name");
    }
    {
        EditableName badge;
        badge.setSize (180, 24);
        badge.setFallback (pair_labels::unpaired);
        badge.setEnabledTooltip (pair_labels::choose);
        badge.onSelect = [] {};
        juce::Image pixels (juce::Image::ARGB, 180, 24, true);
        juce::Graphics graphics (pixels);
        text_style::ShownTextLog shown;
        {
            i18n::ScopedLanguage language (i18n::Language::english);
            badge.setModelName ({});
            badge.paint (graphics);
            require (shown.texts().contains ("NO PRE"), "unpaired badge shows NO PRE");
            require (badge.getTooltip() == "Choose PRE", "tooltip stays concise");
            shown.clear(); badge.setModelName ("Studio"); badge.paint (graphics);
            require (shown.texts().contains ("PRE  Studio"), "paired badge shows PRE and its name");
        }
        shown.clear();
        {
            i18n::ScopedLanguage language (i18n::Language::japanese);
            badge.setModelName ({}); badge.paint (graphics);
            require (shown.texts().contains (juce::String::fromUTF8 (u8"PREなし")), "Japanese unpaired badge");
        }
    }
    EditableName name;
    name.setPresentationContext (presentation::forEditor (900,600)); name.setSize (300,30);
    name.setModelName ({}); name.setFallback ("SELECT PRE");
    bool selected = false, demanded = false;
    name.onSelect = [&] { selected = true; }; name.onPreviewDemand = [&] { demanded = true; };
    require (name.setSelectionPreview ("+ PRE 01234567",7), "the full explicit identity label fits");
    require (name.paintedSelectionGeneration() == 0 && !selected, "receiving a result cannot authorize an unseen label or connect");
    juce::Image image (juce::Image::ARGB,300,30,true); juce::Graphics graphics (image); name.paint (graphics);
    require (name.paintedSelectionGeneration() == 7,"painting acknowledges this exact preview generation");
    name.focusGained (juce::Component::focusChangedByTabKey);
    require (demanded && !selected,"keyboard focus requests evidence but never connects");
    require (name.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)) && selected,"keyboard selection has a direct action");
    name.setSelectionPreview ("+ PRE 76543210",8);
    require (name.paintedSelectionGeneration() == 0,"changed generation invalidates the painted receipt");
    name.setSize (70,30);
    name.paint (graphics); require (name.paintedSelectionGeneration() == 0,"resize invalidates a preview before the next timer update");
    require (!name.setSelectionPreview ("+ PRE 76543210",8),"narrow labels retain the ordinary menu instead of truncating identity");
    name.paint (graphics); require (name.paintedSelectionGeneration() == 0,"a clipped preview cannot be selected as displayed");
}
}
