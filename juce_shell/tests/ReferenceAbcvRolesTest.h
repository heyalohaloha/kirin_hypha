#pragma once

// H10: A／B／C／V の 4 つのボタン（左から A B C V）と、B（REF）の画面の B SET・曲の選択。
// B の画面には V・C の選択を出さない。B が鳴らせないときは押すと理由を言う。
#include "ReferenceGuideContractTest.h"

namespace hypha::tests
{
inline void verifyReferenceAbcvRoles()
{
    using namespace reference_guide_contract;
    for (const auto& size : observatory::sizePresets)
    {
        observatory::View shell (observatory::Role::post);
        shell.setSize (size.width, size.height);
        shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        reference_ui::Component panel;
        panel.setVisible (true);
        panel.setPresentationContext (presentation::forEditor (size.width, size.height));
        panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
        auto state = named ("ready");
        state.songSets = { { "set-1", "Mastering refs   1 / 2" }, { "set-2", "Loud   2 / 2" } };
        state.songSetId = "set-1";
        state.songs = { { "e1/e1/song-1", "Hello" }, { "e2/e2/song-2", "MONTERO   PREPARING" } };
        state.songId = "e1/e1/song-1";
        state.referenceReady = state.referenceArmable = true;
        state.referenceStep = reference_ui::SourceStep::ready;
        state.comparisonSlot = 3;
        panel.setState (state);
        auto* a = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-a"));
        auto* b = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-ref"));
        auto* c = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-c"));
        auto* v = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-b"));
        require (a && b && c && v, "A, B, C and V buttons exist");
        for (auto* button : { a, b, c, v })
            require (button->isVisible() && ! button->getBounds().isEmpty()
                         && panel.getLocalBounds().contains (button->getBounds()),
                     "every role button is reachable at " + juce::String (size.width));
        require (a->getX() < b->getX() && b->getX() < c->getX() && c->getX() < v->getX()
                     && b->getButtonText() == "B" && v->getButtonText() == "V",
                 "the buttons read A B C V from the left");
        auto* set = dynamic_cast<juce::ComboBox*> (panel.findChildWithID ("reference-song-set"));
        auto* song = dynamic_cast<juce::ComboBox*> (panel.findChildWithID ("reference-song"));
        auto* version = panel.findChildWithID ("reference-version");
        auto* check = panel.findChildWithID ("reference-check");
        require (set && song && version && check, "the B and V/C selectors exist");
        require (set->isVisible() && song->isVisible() && ! version->isVisible() && ! check->isVisible()
                     && song->getText() == "Hello" && set->getText() == "Mastering refs   1 / 2",
                 "the B page shows only the B set and its songs");
        // KIRIN_REFERENCE_UI_ABCV_OUTPUT=<dir>：寸法ごとの B の画面を PNG に書き出す（見た目の確認用）。
        if (const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_UI_ABCV_OUTPUT", {});
            directory.isNotEmpty())
        {
            juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
            juce::Graphics graphics (image);
            graphics.fillAll (juce::Colour (0xff16110d));
            panel.paintEntireComponent (graphics, true);
            juce::FileOutputStream stream { juce::File (directory).getChildFile ("abcv_b_" + juce::String (size.width) + ".png") };
            if (stream.openedOk()) { stream.setPosition (0); stream.truncate(); juce::PNGImageFormat().writeImageToStream (image, stream); }
        }

        int auditions = 0;
        juce::String chosenSong, chosenSet, explained;
        panel.onSelectRef = [&] { ++auditions; };
        panel.onSelectSong = [&] (const juce::String& id) { chosenSong = id; };
        panel.onSelectSongSet = [&] (const juce::String& id) { chosenSet = id; };
        panel.onExplain = [&] (const juce::String& reason) { explained = reason; };
        b->onClick();
        song->setSelectedId (2, juce::sendNotificationSync);
        set->setSelectedId (2, juce::sendNotificationSync);
        require (auditions == 1 && chosenSong == "e2/e2/song-2" && chosenSet == "set-2" && explained.isEmpty(),
                 "B plays, and a song or a set is chosen on the B page");

        auto empty = state;
        empty.songSets.clear(); empty.songs.clear(); empty.songId.clear(); empty.songSetId.clear();
        empty.referenceReady = empty.referenceArmable = false;
        panel.setState (empty);
        b->onClick();
        require (auditions == 1 && explained == "B: Rank a B set for Hypha in Kirin OS",
                 "a B that cannot play says why instead of doing nothing");

        state.comparisonSlot = 1;
        panel.setState (state);
        require (! set->isVisible() && ! song->isVisible() && version->isVisible(),
                 "the V page keeps its own selectors");
        panel.onSelectRef = {}; panel.onSelectSong = {}; panel.onSelectSongSet = {}; panel.onExplain = {};
    }
}
}
