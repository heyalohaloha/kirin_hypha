#pragma once

// H10: A／B／C／V の 4 つのボタン（左から A B C V）と、B（REF）の画面の B SET・曲の選択。
// B の画面には V・C の選択を出さない。B が鳴らせないときは押すと理由を言う。
#include "ReferenceGuideContractTest.h"
#include "ReferenceStatusLineTest.h"
#include "ReferenceCheckPageTest.h"
#include "ReferenceBlauertTest.h"
#include "ReferenceAComparisonTest.h"

namespace hypha::tests
{
inline void verifyReferenceAbcvRoles()
{
    using namespace reference_guide_contract;
    verifyReferenceStatusLine();
    verifyReferenceCheckPage();
    verifyReferenceBlauertReadout();
    verifyReferenceAComparison();
    // 2026-10-03（X3）：再生中でも、準備が自動で進む段階なら押した役を待たせる（押したことを捨てない）。
    // 利用者が動かす段階（Cue の外・この区間で合わない）は待たせず、理由を言う。
    {
        auto playing = named ("ready");
        playing.separateComparisons = playing.libraryReceived = playing.transportPlaying = true;
        playing.osAccess = os_access::State::ready;
        playing.referenceArmable = playing.versionArmable = false;
        playing.referenceStep = reference_ui::SourceStep::loadingAudio;
        playing.versionStep = reference_ui::SourceStep::aligning;
        require (reference_ui::canQueueReference (playing) && reference_ui::canQueueSource (playing, true),
                 "pressing B or V while it loads or aligns waits for it");
        playing.referenceStep = reference_ui::SourceStep::outsideCue;
        playing.versionStep = reference_ui::SourceStep::noMatchingPassage;
        require (! reference_ui::canQueueReference (playing) && ! reference_ui::canQueueSource (playing, true),
                 "a step the person must change is explained instead of waited for");
    }
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
        // H11: 曲の Kirin OS の値（Cue の LUFS-I と 12 帯域のスペクトル）。
        for (const auto& [lufs, tilt, prepared] : { std::tuple { -9.4, 0.0f, true }, std::tuple { -12.1, -6.0f, false } })
        {
            reference_ui::SongFact fact;
            fact.lufsI = lufs; fact.prepared = prepared;
            for (int band = 0; band < 12; ++band)
            {
                fact.centersHz.push_back (25.0 * std::pow (1.8, band));
                fact.medianDb.push_back (-30.0f - 2.5f * static_cast<float> (band) + tilt * static_cast<float> (band) / 11.0f);
            }
            state.songFacts.push_back (fact);
        }
        state.songFacts[1].preparation = { "pending", "queued", {}, {}, "working", 3 };  // K13b：Kirin OS が先に 3 曲を準備中
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
        // 100%（300×200）は曲名だけ。B SET は出さず、曲は 125% 以上で選ぶ（H10）。
        const bool glance = presentation::forEditor (size.width, size.height).density == observatory::Density::compact;
        require (set->isVisible() == ! glance && song->isVisible() && ! version->isVisible() && ! check->isVisible()
                     && song->getText() == "Hello" && set->getText() == "Mastering refs   1 / 2"
                     && song->isEnabled() == ! glance,
                 "the B page shows only the B set and its songs, and only the song at 100%");
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

        auto* list = dynamic_cast<reference_ui::SongList*> (panel.findChildWithID ("reference-song-list"));
        require (list != nullptr, "the B song list exists");
        if (list->isVisible())
        {
            require (list->rows().size() == 2 && list->rows()[0].selected && list->rows()[0].title == "Hello"
                         && list->rows()[1].preparing && list->rows()[1].title == "MONTERO" && list->rows()[1].preparation == "3 AHEAD"
                         && std::abs (list->rows()[0].lufsI + 9.4) < 1.0e-9,
                     "the B page lists the songs with their Kirin OS loudness and state");
            juce::String listed;
            list->onChoose = [&] (const juce::String& id) { listed = id; };
            list->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 20.0f, 6.0f + 18.0f + 30.0f + 4.0f },
                juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, list, list, juce::Time(), { 20.0f, 58.0f }, juce::Time(), 1, false));
            require (listed == "e2/e2/song-2", "a row press chooses that song");
            // 2026-10-04：見出しの下の A の行（直近の窓、押せない）と、鳴っていない曲の押したときの gain（A の窓 − Cue の値）。
            {
                auto live = state;
                live.aWindowLoudness = -11.0;
                live.aWindowBlocks = 100;
                panel.setState (live);
                require (std::abs (list->rows()[0].gainDb + 1.6) < 1.0e-9 && ! std::isfinite (list->rows()[1].gainDb),
                         "every prepared B song shows the gain it would play at");
                listed.clear();
                list->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 20.0f, 96.0f },
                    juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, list, list, juce::Time(), { 20.0f, 96.0f }, juce::Time(), 1, false));
                require (listed == "e2/e2/song-2", "with A's row on top, a press still chooses the song under it");
                listed.clear();
                list->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 20.0f, 30.0f },
                    juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, list, list, juce::Time(), { 20.0f, 30.0f }, juce::Time(), 1, false));
                require (listed.isEmpty(), "A's row is not a song to choose");
                panel.setState (state);
            }
            // 見出し（上の 18 px）と、行の無い下の余白を押しても選ばない。
            for (const auto y : { 6.0f + 9.0f, 6.0f + 18.0f + 2.0f * 34.0f + 5.0f })  // 見出しと、2 行の下
            {
                listed.clear();
                list->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 20.0f, y },
                    juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, list, list, juce::Time(), { 20.0f, y }, juce::Time(), 1, false));
                require (listed.isEmpty(), "pressing the header or the empty space chooses no song");
            }
            list->onChoose = [&panel] (const juce::String& id) { if (panel.onSelectSong) panel.onSelectSong (id); };
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

        // H10: 300% 未満の C と V は薄く（理由の代わりに「300% で開く」）、押すと 300% に広げてその役の画面を
        // 開くだけで、音は変えない。300% では今までどおり鳴らす。
        auto sized = state;
        sized.blindLargeScreen = size.width >= 900;
        panel.setState (sized);
        int opened = 0, visual = 0;
        bool heard = false;
        panel.onOpenLarge = [&] (int slot) { opened = slot; };
        panel.onSelectVisualSlot = [&] (int slot) { visual = slot; };
        panel.onSelectB = [&] { heard = true; };
        panel.onSelectC = [&] { heard = true; };
        c->onClick();
        const auto openedC = opened, visualC = visual;
        opened = visual = 0;
        v->onClick();
        if (sized.blindLargeScreen)
            require (opened == 0 && v->getTooltip() != "Open V at 300%", "at 300% V and C play as before");
        else
            require (openedC == 2 && visualC == 2 && opened == 1 && visual == 1 && ! heard
                         && c->getTooltip() == "Open C at 300%" && v->getTooltip() == "Open V at 300%",
                     "below 300% V and C open their page at 300% without changing the sound");
        panel.onOpenLarge = {}; panel.onSelectVisualSlot = {}; panel.onSelectB = {}; panel.onSelectC = {};
        panel.onSelectRef = {}; panel.onSelectSong = {}; panel.onSelectSongSet = {}; panel.onExplain = {};
    }
}
}
